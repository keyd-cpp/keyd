/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include "keyd.h"
#include <algorithm>

static int64_t process_event(struct keyboard *kbd, uint16_t code, int pressed, int64_t time);

/*
 * Here be tiny dragons.
 */

static int64_t get_time()
{
	/* Close enough :/. Using a syscall is unnecessary. */
	static int64_t time = 1;
	return time++;
}

static int cache_set(struct keyboard *kbd, uint16_t code, struct cache_entry *ent)
{
	size_t i;
	int slot = -1;

	for (i = 0; i < CACHE_SIZE; i++)
		if (kbd->cache[i].code == code) {
			slot = i;
			break;
		} else if (!kbd->cache[i].code) {
			slot = i;
		}

	if (slot == -1)
		return -1;

	if (ent == NULL) {
		kbd->cache[slot].code = 0;
	} else {
		kbd->cache[slot] = *ent;
		kbd->cache[slot].code = code;
	}

	return 0;
}

static struct cache_entry *cache_get(struct keyboard *kbd, uint16_t code)
{
	size_t i;

	for (i = 0; i < CACHE_SIZE; i++)
		if (kbd->cache[i].code == code)
			return &kbd->cache[i];

	return NULL;
}

static void reset_keystate(struct keyboard *kbd)
{
	for (size_t i = 0; i < kbd->keystate.size(); i++) {
		if (kbd->keystate[i]) {
			kbd->output.send_key(i, 0);
			kbd->keystate[i] = 0;
		}
	}
}

static void send_key(struct keyboard *kbd, uint16_t code, uint8_t pressed)
{
	if (code == KEYD_NOOP)
		return;
	if (code >= kbd->keystate.size()) {
		keyd_log("send_key(): invalid code %u", code);
		return;
	}

	if (pressed)
		kbd->last_pressed_output_code = code;

	if (kbd->keystate[code] != pressed) {
		kbd->keystate[code] = pressed;
		kbd->output.send_key(code, pressed);
	}
}

static void clear_mod(struct keyboard *kbd, uint16_t code)
{
	/*
	 * Some modifiers have a special meaning when used in
	 * isolation (e.g meta in Gnome, alt in Firefox).
	 * In order to prevent spurious key presses we
	 * avoid adjacent down/up pairs by interposing
	 * additional control sequences.
	 */
	int guard = (((kbd->last_pressed_output_code == code) &&
			(code == KEY_LEFTMETA ||
			 code == KEY_LEFTALT ||
			 code == KEY_RIGHTALT)) &&
		       !kbd->inhibit_modifier_guard &&
		       !kbd->config.disable_modifier_guard);

	if (guard && !kbd->keystate[KEY_LEFTCTRL]) {
		send_key(kbd, KEY_LEFTCTRL, 1);
		send_key(kbd, code, 0);
		send_key(kbd, KEY_LEFTCTRL, 0);
	} else {
		send_key(kbd, code, 0);
	}
}

static void set_mods(struct keyboard *kbd, uint8_t mods)
{
	for (size_t i = 0; i < MAX_MOD; i++) {
		uint8_t mask = 1 << i;
		auto& codes = kbd->config.modifiers[i];

		if (mask & mods) {
			// Choose real keys instead (TODO: properly manage physical keys)
			for (uint16_t code : codes) {
				if (kbd->capstate[code] && !kbd->keystate[code])
					send_key(kbd, code, 1);
				if (!kbd->capstate[code] && kbd->keystate[code] && code != codes[0])
					send_key(kbd, code, 0);
			}
			// Check if already active
			if (kbd->keystate[KEYD_FAKEMOD + i])
				continue;
			if (std::any_of(codes.begin(), codes.end(), [&](uint16_t c) { return kbd->keystate[c]; }))
				continue;
			if (codes)
				send_key(kbd, codes[0], 1);
		} else {
			// Clear all possible keys for this mod
			kbd->keystate[KEYD_FAKEMOD + i] = 0;
			for (uint16_t code : codes)
				if (kbd->keystate[code])
					clear_mod(kbd, code);
		}
	}
}

static void update_mods(struct keyboard *kbd, [[maybe_unused]] int excl, uint8_t mods, uint8_t wildcard = -1, uint16_t code = -1)
{
	struct layer *excluded_layer = nullptr;
	if (kbd->config.compat && excl >= 0)
		excluded_layer = &kbd->config.layers.at(excl);
	if (kbd->config.compat)
		wildcard = -1;

	uint8_t addm = 0;
	for (size_t i = 1; i <= MAX_MOD; i++) {
		struct layer *layer = &kbd->config.layers[i];
		size_t excluded = 0;

		if (!kbd->layer_state[i].active())
			continue;

		if (layer == excluded_layer) {
			excluded = 1;
		} else if (excluded_layer) {
			for (auto j : *excluded_layer) {
				if (j == i) {
					excluded = 1;
					break;
				}
			}
		}

		if (!excluded)
			mods |= 1 << (i - 1);
	}

	for (size_t i = 0; i < CACHE_SIZE; i++) {
		// Check active keysequences for mods being active or suppressed
		if (auto& ce = kbd->cache[i]; ce.code && ce.d.op == OP_KEYSEQUENCE) {
			if (ce.d.args[0].code == code)
				continue;
			uint8_t c_wildc = ce.d.args[2].wildc;
			uint8_t c_mods = ce.d.args[1].mods;
			addm |= c_mods & ~c_wildc; // Required mods
			wildcard &= c_wildc; // Least common wildcard
		}
	}
	set_mods(kbd, (mods & wildcard) | addm);
}

static uint8_t get_mods(struct keyboard* kbd)
{
	uint8_t mods = 0;

	for (size_t i = 0; i < MAX_MOD; i++) {
		uint8_t mask = 1 << i;
		if (kbd->layer_state[i + 1].active())
			mods |= mask;
		if (kbd->keystate[KEYD_FAKEMOD + i])
			mods |= mask;
	}

	return mods;
}

static uint8_t what_mods(struct keyboard* kbd, uint16_t code)
{
	uint8_t mods = 0;

	for (size_t i = 0; i < kbd->config.modifiers.size(); i++) {
		uint8_t mask = 1 << i;
		if (kbd->config.is_mod(i, code))
			mods |= mask;
	}

	return mods;
}

static uint64_t execute_macro(struct keyboard *kbd, int16_t dl, uint16_t idx, uint16_t orig_code)
{
	auto& macro = kbd->config.macros[idx & INT16_MAX];
	/* Minimize redundant modifier strokes for simple key sequences. */
	if (macro.size == 1 && macro[0].type <= MACRO_KEY_TAP) {
		uint16_t code = macro[0].id;
		// autokey
		if (!code)
			code = orig_code;

		update_mods(kbd, dl, macro[0].mods.mods, macro[0].mods.wildc);
		send_key(kbd, code, 1);
		send_key(kbd, code, 0);
		return 0;
	} else {
		// Completely disable mods if no wildcard is set
		update_mods(kbd, dl, 0, (kbd->config.compat || idx & 0x8000) ? 0xff : 0);
		return macro_execute(kbd->output.send_key, macro, kbd->config.macro_sequence_timeout, &kbd->config) / 1000;
	}
}

static void lookup_descriptor(struct keyboard *kbd, uint16_t code, struct descriptor *d, int16_t* dl)
{
	d->op = OP_NULL;

	uint64_t maxts = 0;

	if (code >= KEYD_CHORD_1 && code <= KEYD_CHORD_MAX) {
		size_t idx = code - KEYD_CHORD_1;

		*d = kbd->active_chords[idx].chord.d;
		*dl = kbd->active_chords[idx].layer;

		return;
	}

	// Synthesize default key for matching
	descriptor desc{
		.op = OP_KEYSEQUENCE,
		.id = code,
		.mods = get_mods(kbd),
		.wildcard = 0,
		.args = {},
	};

	desc.args[0].code = desc.id;
	desc.args[1].mods = desc.mods;
	desc.args[2].wildc = 0xff;

	size_t set = 0;
	size_t max = 0;
	size_t conflicts = 0;

	for (size_t i = 0; i < kbd->config.layers.size(); i++) {
		struct layer *layer = &kbd->config.layers[i];

		if (kbd->layer_state[i].active()) {
			const auto act_ts = kbd->layer_state[i].activation_time;
			if (i > 0)
				kbd->active_layers[set++] = i;
			if (act_ts < maxts)
				continue;
			if (auto match = layer->keymap[desc]) {
				if (maxts < act_ts)
					conflicts = 0;
				maxts = act_ts;
				max = 1;
				// Check for conflicting matches, actual conflict discards both
				// Deep comparison is performed to verify conflict
				// To avoid some legitimate cases with identical ops
				if (!conflicts || !d->equals(&kbd->config, match))
					conflicts++;
				*d = match;
				*dl = i;
			}
		}
	}

	/* Scan for any composite matches (which take precedence). */
	for (size_t i = MAX_MOD + 1; i < kbd->config.layers.size(); i++) {
		if (set <= 1) [[likely]]
			break;
		// Optimization: don't access uninteresting layers
		if (kbd->layer_state[i].composite == 0)
			continue;
		struct layer *layer = &kbd->config.layers[i];
		if (layer->size() > set || layer->size() < max)
			continue;
		if (!std::includes(kbd->active_layers.begin(), kbd->active_layers.begin() + set, layer->begin(), layer->end()))
			continue;
		if (auto match = layer->keymap[desc]) {
			if (max < layer->size())
				conflicts = 0;
			max = layer->size();
			if (!conflicts || !d->equals(&kbd->config, match))
				conflicts++;
			*d = match;
			*dl = i;
		}
	}

	if (d->op == OP_NULL || conflicts > 1) {
		// If key is a registered modifier, fallback to setting layer by default
		for (size_t i = 0; i < MAX_MOD; i++) {
			if (kbd->config.is_mod(i, code)) {
				desc.op = OP_LAYER;
				desc.args[0].idx = i + 1;
				break;
			}
		}

		*d = desc;
		*dl = 0;
	}
}

static void activate_layer(struct keyboard *kbd, uint16_t code, int idx);

static void deactivate_layer(struct keyboard *kbd, int idx)
{
	// Don't deactivate main
	if (idx == 0)
		return;
	if (idx < 0)
		return activate_layer(kbd, 0, -idx);

	::layer& layer = kbd->config.layers.at(idx);
	if (layer.name) {
		dbg("Deactivating layer %s", layer.name.c_str());
		kbd->layer_state[idx].active_s--;
	} else {
		for (uint16_t i : layer) {
			dbg("Deactivating layer %s", kbd->config.layers[i].name.c_str());
			kbd->layer_state[i].active_s--;
		}
	}

	kbd->output.on_layer_change(kbd, &layer, 0);
}

/*
 * NOTE: Every activation call *must* be paired with a
 * corresponding deactivation call.
 */

static void activate_layer(struct keyboard *kbd, uint16_t code, int idx)
{
	// Don't activate main
	if (idx == 0)
		return;
	if (idx < 0)
		return deactivate_layer(kbd, -idx);

	::layer& layer = kbd->config.layers.at(idx);
	struct cache_entry *ce;

	const auto ts = get_time();
	if (layer.name) {
		dbg("Activating layer %s", layer.name.c_str());
		kbd->layer_state[idx].active_s++;
		if (kbd->layer_state[idx].active())
			kbd->layer_state[idx].activation_time = ts;
	} else {
		for (uint16_t i : layer) {
			dbg("Activating layer %s", kbd->config.layers[i].name.c_str());
			auto& state = kbd->layer_state[i];
			state.active_s++;
			if (state.active())
				state.activation_time = ts;
		}
	}

	if ((ce = cache_get(kbd, code)))
		ce->layer = idx;

	kbd->output.on_layer_change(kbd, &layer, 1);
}

/* Returns:
 *  0 on no match
 *  1 on partial match
 *  2 on exact match
 */
static int chord_event_match(struct chord *chord, struct key_event *events, size_t nevents)
{
	size_t i, j;
	size_t n = 0;
	size_t npressed = 0;

	if (!nevents)
		return 0;

	for (i = 0; i < nevents; i++)
		if (events[i].pressed) {
			int found = 0;

			npressed++;
			for (j = 0; j < chord->keys.size(); j++)
				if (chord->keys[j] == events[i].code)
					found = 1;

			if (!found)
				return 0;
			else
				n++;
		}

	if (npressed == 0)
		return 0;
	else
		return n == (chord->keys.size() - std::count(chord->keys.begin(), chord->keys.end(), 0)) ? 2 : 1;
}

static void enqueue_chord_event(struct keyboard *kbd, uint16_t code, uint8_t pressed, int64_t time)
{
	if (!code)
		return;

	assert(kbd->chord.queue_sz < ARRAY_SIZE(kbd->chord.queue));

	kbd->chord.queue[kbd->chord.queue_sz].code = code;
	kbd->chord.queue[kbd->chord.queue_sz].pressed = pressed;
	kbd->chord.queue[kbd->chord.queue_sz].timestamp = time;

	kbd->chord.queue_sz++;
}

/* Returns:
 *  0 in the case of no match
 *  1 in the case of a partial match
 *  2 in the case of an unambiguous match (populating chord and layer)
 *  3 in the case of an ambiguous match (populating chord and layer)
 */
static int check_chord_match(struct keyboard *kbd, const struct chord **chord, int *chord_layer)
{
	size_t idx;
	int full_match = 0;
	int partial_match = 0;
	int64_t maxts = -1;

	for (idx = 0; idx < kbd->config.layers.size(); idx++) {
		struct layer *layer = &kbd->config.layers[idx];

		if (!kbd->layer_state[idx].composite && !kbd->layer_state[idx].active())
			continue;
		if (kbd->layer_state[idx].composite) {
			if (std::any_of(layer->begin(), layer->end(), [&](auto i) { return !kbd->layer_state[i].active(); }))
				continue;
		}

		for (size_t i = 0; i < layer->chords.size(); i++) {
			int ret = chord_event_match(&layer->chords[i],
						    kbd->chord.queue,
						    kbd->chord.queue_sz);

			if (ret == 2 &&
				maxts <= int64_t(kbd->layer_state[idx].activation_time)) {
				*chord_layer = (int)idx;
				*chord = &layer->chords[i];

				full_match = 1;
				maxts = kbd->layer_state[idx].activation_time;
			} else if (ret == 1) {
				partial_match = 1;
			}
		}
	}

	if (full_match)
		return partial_match ? 3 : 2;
	else if (partial_match)
		return 1;
	else
		return 0;
}

void execute_command(ucmd& cmd)
{
	dbg("executing command: %s", cmd.cmd.c_str());

	if (fork()) {
		return;
	}

	if (cmd.env && cmd.env->gid && setgid(cmd.env->gid) < 0) {
		perror("setgid");
		exit(-1);
	}
	if (cmd.env && cmd.env->uid && setuid(cmd.env->uid) < 0) {
		perror("setuid");
		exit(-1);
	}

	int fd = open("/dev/null", O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(-1);
	}

	close(0);
	close(1);
	close(2);

	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);

	if (auto env = cmd.env.get(); env && env->env)
		execle("/bin/sh", "/bin/sh", "-c", cmd.cmd.c_str(), nullptr, env->env.get());
	else
		execl("/bin/sh", "/bin/sh", "-c", cmd.cmd.c_str(), nullptr);
}

static void clear_oneshot(struct keyboard *kbd, [[maybe_unused]] const char* reason)
{
	size_t i = 0;

	for (i = 0; i < kbd->config.layers.size(); i++)
		while (kbd->layer_state[i].oneshot_depth) {
			deactivate_layer(kbd, i);
			kbd->layer_state[i].oneshot_depth--;
		}

	kbd->oneshot_latch = 0;
	kbd->oneshot_timeout = 0;
}

static void clear(struct keyboard *kbd)
{
	clear_oneshot(kbd, "clear");
	for (size_t i = 1; i < kbd->config.layers.size(); i++) {
		struct layer *layer = &kbd->config.layers[i];

		if (i != size_t(kbd->layout)) {
			if (kbd->layer_state[i].toggled) {
				kbd->layer_state[i].toggled = 0;
				deactivate_layer(kbd, i);
			}
		}
	}

	kbd->active_macro = -1;

	reset_keystate(kbd);
}

static void setlayout(struct keyboard *kbd, int idx)
{
	clear(kbd);

	// Setting the layout to main is equivalent to clearing all occluding layouts.
	if (kbd->layout) {
		// TODO: this may not actually work as expected
		kbd->layer_state[kbd->layout].active_s--;
	}
	if (idx) {
		kbd->layer_state[idx].active_s++;
		kbd->layer_state[idx].activation_time = 1;
	}
	kbd->layout = idx;
	kbd->output.on_layer_change(kbd, &kbd->config.layers[idx], 1);
}


static void schedule_timeout(struct keyboard *kbd, int64_t timeout)
{
	assert(kbd->nr_timeouts < ARRAY_SIZE(kbd->timeouts));
	kbd->timeouts[kbd->nr_timeouts++] = timeout;
}

static int64_t calculate_main_loop_timeout(struct keyboard *kbd, int64_t time)
{
	size_t i;
	int64_t timeout = 0;
	size_t n = 0;

	for (i = 0; i < kbd->nr_timeouts; i++)
		if (kbd->timeouts[i] > time) {
			if (!timeout || kbd->timeouts[i] < timeout)
				timeout = kbd->timeouts[i];

			kbd->timeouts[n++] = kbd->timeouts[i];
		}

	kbd->nr_timeouts = n;
	return timeout ? timeout - time : 0;
}

static void do_keysequence(struct keyboard *kbd, int16_t dl, int pressed, int64_t time, uint16_t code, uint8_t mods, uint8_t wildcard)
{
	if (pressed) {
		if (kbd->keystate[code])
			send_key(kbd, code, 0);

		update_mods(kbd, dl, mods, wildcard | mods, code);
		send_key(kbd, code, 1);
		clear_oneshot(kbd, "key");
	} else {
		send_key(kbd, code, 0);
		update_mods(kbd, -1, 0);
	}

	if (!mods || mods == (1 << MOD_SHIFT))
		kbd->last_simple_key_time = time;
}

static int64_t process_descriptor(struct keyboard *kbd, uint16_t code, const struct descriptor *d, int16_t dl, int pressed, int64_t time)
{
	int64_t timeout = 0;

	switch (d->op) {
	case OP_CLEARM:
		if (pressed)
			clear(kbd);
		[[fallthrough]];
	case OP_LAYERM:
	case OP_ONESHOTM:
	case OP_TOGGLEM:
	case OP_OVERLOADM: {
		uint16_t macro_code = d->args[d->op != OP_CLEARM].code;
		auto& macro = kbd->config.macros[macro_code & INT16_MAX];
		if (macro.size == 1 && macro[0].type == MACRO_KEY_SEQ && !kbd->config.compat) {
			// Try to behave like an OP_KEYSEQUENCE
			// Otherwise the duck weirdly doesn't swim
			// The only missing piece is OP_SWAPM mess (TODO)
			uint16_t new_code = macro[0].id;
			// Autokey
			if (!new_code)
				new_code = code;
			do_keysequence(kbd, dl, pressed, time, new_code, macro[0].mods.mods, macro[0].mods.wildc);
		} else if (pressed) {
			// Proceed normally
			execute_macro(kbd, dl, macro_code, code);
		}
		break;
	}
	default:
		break;
	}

	auto auto_layer = [&]() -> int {
		// Infer layer index from the keycode
		uint8_t x = what_mods(kbd, code);
		if (std::popcount(x) == 1) [[likely]] {
			return std::countr_zero(x) + 1;
		} else {
			return x << 16;
		}
	};

	switch (d->op) {
		int idx;
		struct descriptor *action;
		uint16_t new_code;

	case OP_KEYSEQUENCE:
		new_code = d->args[0].code;
		if (!new_code)
			new_code = code;

		do_keysequence(kbd, dl, pressed, time, new_code, d->args[1].mods, d->args[2].wildc);
		break;
	case OP_SCROLL:
		kbd->scroll.sensitivity = d->args[0].sensitivity;
		if (pressed)
			kbd->scroll.active = 1;
		else
			kbd->scroll.active = 0;
		break;
	case OP_SCROLL_TOGGLE:
		kbd->scroll.sensitivity = d->args[0].sensitivity;
		if (pressed)
			kbd->scroll.active = !kbd->scroll.active;
		break;
	case OP_OVERLOAD_IDLE_TIMEOUT:
		if (pressed) {
			int64_t timeout = d->args[2].timeout;

			if (((time - kbd->last_simple_key_time) >= timeout))
				action = &kbd->config.descriptors[d->args[1].idx];
			else
				action = &kbd->config.descriptors[d->args[0].idx];

			process_descriptor(kbd, code, action, dl, 1, time);
			for (int i = 0; i < CACHE_SIZE; i++) {
				if (code == kbd->cache[i].code) {
					kbd->cache[i].d = *action;
					break;
				}
			}
		}
		break;
	case OP_OVERLOAD_TIMEOUT_TAP:
	case OP_OVERLOAD_TIMEOUT:
		if (pressed) {
			int16_t layer = d->args[0].idx;

			kbd->pending_key.code = code;
			kbd->pending_key.behaviour =
				d->op == OP_OVERLOAD_TIMEOUT_TAP ?
					PK_UNINTERRUPTIBLE_TAP_ACTION2 :
					PK_UNINTERRUPTIBLE;

			kbd->pending_key.dl = dl;
			kbd->pending_key.action1 = kbd->config.descriptors[d->args[1].idx];
			kbd->pending_key.action2.op = OP_LAYER;
			kbd->pending_key.action2.args[0].idx = layer;
			kbd->pending_key.expire = time + d->args[2].timeout;

			schedule_timeout(kbd, kbd->pending_key.expire);
		}

		break;
	case OP_LAYOUT:
		idx = d->args[0].idx;
		if (idx < 0)
			break;
		if (pressed)
			setlayout(kbd, idx);

		break;
	case OP_LAYERM:
	case OP_LAYER:
		idx = d->args[0].idx;
		if (idx == INT16_MIN)
			idx = 0;
		else if (!idx)
			idx = auto_layer();


		// Allow disabling layer with prefix '-'
		if (pressed) {
			activate_layer(kbd, code, idx);
		} else {
			deactivate_layer(kbd, idx);
		}

		if (kbd->last_pressed_code == code) {
			kbd->inhibit_modifier_guard = 1;
			update_mods(kbd, -1, 0);
			kbd->inhibit_modifier_guard = 0;
		} else {
			update_mods(kbd, -1, 0);
		}

		break;
	case OP_CLEARM:
		break;
	case OP_CLEAR:
		if(pressed)
			clear(kbd);
		break;
	case OP_OVERLOAD:
	case OP_OVERLOADM:
		idx = d->args[0].idx;
		action = &kbd->config.descriptors[d->args[d->op == OP_OVERLOADM ? 2 : 1].idx];
		if (idx == INT16_MIN)
			idx = 0;
		else if (!idx)
			idx = auto_layer();

		if (pressed) {
			kbd->overload_start_time = time;
			activate_layer(kbd, code, idx);
			update_mods(kbd, -1, 0);
		} else {
			deactivate_layer(kbd, idx);
			update_mods(kbd, -1, 0);

			if (kbd->last_pressed_code == code &&
			    (!kbd->config.overload_tap_timeout ||
			     ((time - kbd->overload_start_time) < kbd->config.overload_tap_timeout))) {
				if (action->op == OP_MACRO) {
					/*
					 * Macro release relies on event logic, so we can't just synthesize a
					 * descriptor release.
					 */
					execute_macro(kbd, dl, action->args[0].code, code);
				} else {
					process_descriptor(kbd, code, action, dl, 1, time);
					process_descriptor(kbd, code, action, dl, 0, time);
				}
			}
		}

		break;
	case OP_ONESHOTM:
	case OP_ONESHOT:
		idx = d->args[0].idx;
		if (idx < 0)
			break;
		if (!idx)
			idx = auto_layer();

		if (pressed) {
			activate_layer(kbd, code, idx);
			update_mods(kbd, dl, 0);
			kbd->oneshot_latch = 1;
		} else {
			if (kbd->oneshot_latch) {
				kbd->layer_state[idx].oneshot_depth++;
				if (kbd->config.oneshot_timeout) {
					kbd->oneshot_timeout = time + kbd->config.oneshot_timeout;
					schedule_timeout(kbd, kbd->oneshot_timeout);
				}
			} else {
				deactivate_layer(kbd, idx);
				update_mods(kbd, -1, 0);
			}
		}

		break;
	case OP_MACRO2:
	case OP_MACRO:
		if (pressed) {
			uint16_t macro_idx;
			if (d->op == OP_MACRO2) {
				macro_idx = d->args[2].code;

				timeout = d->args[0].timeout;
				kbd->macro_repeat_interval = d->args[1].timeout;
			} else {
				macro_idx = d->args[0].code;

				timeout = kbd->config.macro_timeout;
				kbd->macro_repeat_interval = kbd->config.macro_repeat_timeout;
			}

			clear_oneshot(kbd, "macro");

			timeout += execute_macro(kbd, dl, macro_idx, code);
			kbd->active_macro = macro_idx;
			kbd->active_macro_layer = dl;

			kbd->macro_timeout = time + timeout;
			schedule_timeout(kbd, kbd->macro_timeout);
		}

		break;
	case OP_TOGGLEM:
	case OP_TOGGLE:
		idx = d->args[0].idx;
		if (idx == -INT16_MIN)
			break;
		else if (!idx)
			idx = auto_layer();
		else
			idx = abs(idx);

		if (pressed) {
			auto was_toggled = kbd->layer_state[idx].toggled;
			kbd->layer_state[idx].toggled = d->args[0].idx < 0 ? 0 : !was_toggled;

			if (kbd->layer_state[idx].toggled)
				activate_layer(kbd, code, idx);
			else if (was_toggled)
				deactivate_layer(kbd, idx);

			update_mods(kbd, -1, 0);
			clear_oneshot(kbd, "toggle");
		}

		break;
	case OP_TIMEOUT:
		if (pressed) {
			kbd->pending_key.action1 = kbd->config.descriptors[d->args[0].idx];
			kbd->pending_key.action2 = kbd->config.descriptors[d->args[2].idx];

			kbd->pending_key.code = code;
			kbd->pending_key.dl = dl;
			kbd->pending_key.expire = time + d->args[1].timeout;
			kbd->pending_key.behaviour = PK_INTERRUPT_ACTION1;

			schedule_timeout(kbd, kbd->pending_key.expire);
		}

		break;
	case OP_SWAP:
	case OP_SWAPM:
		idx = d->args[0].idx;
		if (idx < 0)
			break;
		if (!idx)
			idx = auto_layer();

		if (pressed) {
			size_t i;
			struct cache_entry *ce = NULL;

			if (kbd->layer_state[dl].toggled) {
				deactivate_layer(kbd, dl);
				kbd->layer_state[dl].toggled = 0;

				activate_layer(kbd, 0, idx);
				kbd->layer_state[idx].toggled = 1;
				update_mods(kbd, -1, 0);
			} else if (kbd->layer_state[dl].oneshot_depth) {
				deactivate_layer(kbd, dl);
				kbd->layer_state[dl].oneshot_depth--;

				activate_layer(kbd, 0, idx);
				kbd->layer_state[idx].oneshot_depth++;
				update_mods(kbd, -1, 0);
			} else {
				for (i = 0; i < CACHE_SIZE; i++) {
					uint16_t code = kbd->cache[i].code;
					int16_t layer = kbd->cache[i].layer;

					if (code && layer == dl && layer != kbd->layout && layer != 0) {
						ce = &kbd->cache[i];
						break;
					}
				}

				if (ce) {
					ce->d.op = OP_LAYER;
					ce->d.args[0].idx = idx;

					deactivate_layer(kbd, dl);
					activate_layer(kbd, ce->code, idx);

					update_mods(kbd, -1, 0);
				}
			}

			if (d->op == OP_SWAPM)
				execute_macro(kbd, dl, d->args[1].code, code);
		} else if (d->op == OP_SWAPM) {
			auto& macro = kbd->config.macros[d->args[1].code & INT16_MAX];
			if (macro.size == 1 && macro[0].type <= MACRO_KEY_TAP) {
				// Why is this necessary?
				send_key(kbd, macro[0].id, 0);
				update_mods(kbd, -1, 0);
			}
		}

		break;
	default:
		keyd_log("Unknown OP code: %u", +static_cast<uint16_t>(d->op));
		return 0;
	}

	if (pressed)
		kbd->last_pressed_code = code;

	return timeout;
}

std::unique_ptr<keyboard> new_keyboard(std::unique_ptr<keyboard> kbd)
{
	size_t i;

	kbd->update_layer_state();
	kbd->layer_state[0].active_s = 1;
	kbd->layer_state[0].activation_time = 0;

	if (kbd->config.default_layout && kbd->config.default_layout != kbd->config.layers[0].name) {
		int found = 0;
		for (i = 1; i < kbd->config.layers.size(); i++) {
			struct layer *layer = &kbd->config.layers[i];

			if (layer->name == kbd->config.default_layout) {
				kbd->layer_state[i].active_s = 1;
				kbd->layer_state[i].activation_time = 1;
				kbd->layout = i;
				found = 1;
				break;
			}
		}

		if (!found)
			keyd_log("\tWARNING: could not find default layout %s.\n",
				kbd->config.default_layout.c_str());
	}

	kbd->chord.queue_sz = 0;
	kbd->chord.state = CHORD_INACTIVE;

	return kbd;
}

static int resolve_chord(struct keyboard *kbd)
{
	size_t queue_offset = 0;
	const struct chord *chord = kbd->chord.match;

	kbd->chord.state = CHORD_RESOLVING;

	if (chord) {
		size_t i;
		uint16_t code = 0;

		for (i = 0; i < ARRAY_SIZE(kbd->active_chords); i++) {
			struct active_chord *ac = &kbd->active_chords[i];
			if (!ac->active) {
				ac->active = 1;
				ac->chord = *chord;
				ac->layer = kbd->chord.match_layer;
				code = KEYD_CHORD_1 + i;

				break;
			}
		}

		assert(code);

		queue_offset = chord->keys.size() - std::count(chord->keys.begin(), chord->keys.end(), 0);
		process_event(kbd, code, 1, kbd->chord.last_code_time);
	}


	kbd_process_events(kbd,
			   kbd->chord.queue + queue_offset,
			   kbd->chord.queue_sz - queue_offset);
	kbd->chord.state = CHORD_INACTIVE;
	return 1;
}

static int abort_chord(struct keyboard *kbd)
{
	kbd->chord.match = NULL;
	return resolve_chord(kbd);
}

static int handle_chord(struct keyboard *kbd, uint16_t code, int pressed, int64_t time)
{
	size_t i;
	const int64_t interkey_timeout = kbd->config.chord_interkey_timeout;
	const int64_t hold_timeout = kbd->config.chord_hold_timeout;

	if (code && !pressed) {
		for (i = 0; i < ARRAY_SIZE(kbd->active_chords); i++) {
			struct active_chord *ac = &kbd->active_chords[i];
			uint8_t chord_code = KEYD_CHORD_1 + i;

			if (ac->active) {
				int nremaining = 0;
				int found = 0;

				for (size_t i = 0; i < ac->chord.keys.size(); i++) {
					if (ac->chord.keys[i] == code) {
						ac->chord.keys[i] = 0;
						found = 1;
					}

					if (ac->chord.keys[i])
						nremaining++;
				}

				if (found) {
					if (nremaining == 0) {
						ac->active = 0;
						process_event(kbd, chord_code, 0, time);
					}

					return 1;
				}
			}
		}
	}

	switch (kbd->chord.state) {
	case CHORD_RESOLVING:
		return 0;
	case CHORD_INACTIVE:
		kbd->chord.queue_sz = 0;
		kbd->chord.match = NULL;
		kbd->chord.start_code = code;

		enqueue_chord_event(kbd, code, pressed, time);
		switch (check_chord_match(kbd, &kbd->chord.match, &kbd->chord.match_layer)) {
			case 0:
				return 0;
			case 3:
			case 1:
				kbd->chord.state = CHORD_PENDING_DISAMBIGUATION;
				kbd->chord.last_code_time = time;
				schedule_timeout(kbd, time + interkey_timeout);
				return 1;
			default:
			case 2:
				kbd->chord.last_code_time = time;

				if (hold_timeout) {
					kbd->chord.state = CHORD_PENDING_HOLD_TIMEOUT;
					schedule_timeout(kbd, time + hold_timeout);
				} else {
					return resolve_chord(kbd);
				}
				return 1;
		}
	case CHORD_PENDING_DISAMBIGUATION:
		if (!code) {
			if ((time - kbd->chord.last_code_time) >= interkey_timeout) {
				if (kbd->chord.match) {
					int64_t timeleft = hold_timeout - interkey_timeout;
					if (timeleft > 0) {
						schedule_timeout(kbd, time + timeleft);
						kbd->chord.state = CHORD_PENDING_HOLD_TIMEOUT;
					} else {
						return resolve_chord(kbd);
					}
				} else {
					return abort_chord(kbd);
				}

				return 1;
			}

			return 0;
		}

		enqueue_chord_event(kbd, code, pressed, time);

		if (!pressed)
			return abort_chord(kbd);

		switch (check_chord_match(kbd, &kbd->chord.match, &kbd->chord.match_layer)) {
			case 0:
				return abort_chord(kbd);
			case 3:
			case 1:
				kbd->chord.last_code_time = time;

				kbd->chord.state = CHORD_PENDING_DISAMBIGUATION;
				schedule_timeout(kbd, time + interkey_timeout);
				return 1;
			default:
			case 2:
				kbd->chord.last_code_time = time;

				if (hold_timeout) {
					kbd->chord.state = CHORD_PENDING_HOLD_TIMEOUT;
					schedule_timeout(kbd, time + hold_timeout);
				} else {
					return resolve_chord(kbd);
				}
				return 1;
		}
	case CHORD_PENDING_HOLD_TIMEOUT:
		if (!code) {
			if ((time - kbd->chord.last_code_time) >= hold_timeout)
				return resolve_chord(kbd);

			return 0;
		}

		enqueue_chord_event(kbd, code, pressed, time);

		if (!pressed) {
			for (size_t i = 0; i < kbd->chord.match->keys.size(); i++)
				if (kbd->chord.match->keys[i] == code)
					return abort_chord(kbd);
		}

		return 1;
	}

	return 0;
}

int handle_pending_key(struct keyboard *kbd, uint16_t code, int pressed, int64_t time)
{
	if (!kbd->pending_key.code)
		return 0;

	struct descriptor action = {};

	if (code) {
		struct key_event *ev;

		assert(kbd->pending_key.queue_sz < ARRAY_SIZE(kbd->pending_key.queue));

		if (!pressed) {
			size_t i;
			int found = 0;

			for (i = 0; i < kbd->pending_key.queue_sz; i++)
				if (kbd->pending_key.queue[i].code == code)
					found = 1;

			/* Propagate key up events for keys which were struck before the pending key. */
			if (!found && code != kbd->pending_key.code)
				return 0;
		}

		ev = &kbd->pending_key.queue[kbd->pending_key.queue_sz];
		ev->code = code;
		ev->pressed = pressed;
		ev->timestamp = time;

		kbd->pending_key.queue_sz++;
	}


	if (time >= kbd->pending_key.expire) {
		action = kbd->pending_key.action2;
	} else if (code == kbd->pending_key.code) {
		if (kbd->pending_key.tap_expiry && time >= kbd->pending_key.tap_expiry) {
			action.op = OP_KEYSEQUENCE;
			action.args[0].code = KEYD_NOOP;
		} else {
			action = kbd->pending_key.action1;
		}
	} else if (code && pressed && kbd->pending_key.behaviour == PK_INTERRUPT_ACTION1) {
		action = kbd->pending_key.action1;
	} else if (code && pressed && kbd->pending_key.behaviour == PK_INTERRUPT_ACTION2) {
		action = kbd->pending_key.action2;
	} else if (kbd->pending_key.behaviour == PK_UNINTERRUPTIBLE_TAP_ACTION2 && !pressed) {
		size_t i;

		for (i = 0; i < kbd->pending_key.queue_sz; i++)
			if (kbd->pending_key.queue[i].code == code) {
				action = kbd->pending_key.action2;
				break;
			}
	}

	if (action.op != OP_NULL) {
		/* Create a copy of the queue on the stack to
		   allow for recursive pending key processing. */
		struct key_event queue[ARRAY_SIZE(kbd->pending_key.queue)];
		size_t queue_sz = kbd->pending_key.queue_sz;

		uint16_t code = kbd->pending_key.code;
		int16_t dl = kbd->pending_key.dl;

		memcpy(queue, kbd->pending_key.queue, sizeof kbd->pending_key.queue);

		kbd->pending_key.code = 0;
		kbd->pending_key.queue_sz = 0;
		kbd->pending_key.tap_expiry = 0;

		struct cache_entry ce = {
			.code = 0,
			.d = action,
			.dl = dl,
			.layer = 0,
		};

		cache_set(kbd, code, &ce);
		process_descriptor(kbd, code, &action, dl, 1, time);

		/* Flush queued events */
		kbd_process_events(kbd, queue, queue_sz);
	}

	return 1;
}

/*
 * `code` may be 0 in the event of a timeout.
 *
 * The return value corresponds to a timeout before which the next invocation
 * of process_event must take place. A return value of 0 permits the
 * main loop to call at liberty.
 */
static int64_t process_event(struct keyboard *kbd, uint16_t code, int pressed, int64_t time)
{
	int dl = -1;

	if (handle_chord(kbd, code, pressed, time))
		goto exit;

	if (handle_pending_key(kbd, code, pressed, time))
		goto exit;

	if (kbd->oneshot_timeout && time >= kbd->oneshot_timeout) {
		clear_oneshot(kbd, "timeout");
		update_mods(kbd, -1, 0);
	}

	if (kbd->active_macro >= 0) {
		if (code) {
			kbd->active_macro = -1;
			update_mods(kbd, -1, 0);
		} else if (time >= kbd->macro_timeout) {
			auto add = execute_macro(kbd, kbd->active_macro_layer, kbd->active_macro, code);
			kbd->macro_timeout = add + time + kbd->macro_repeat_interval;
			schedule_timeout(kbd, kbd->macro_timeout);
		}
	}

	if (code) {
		struct descriptor d;
		int16_t dl = 0;

		if (pressed) {
			/*
			 * Guard against successive key down events
			 * of the same key code. This can be caused
			 * by unorthodox hardware or by different
			 * devices mapped to the same config.
			 */
			if (cache_get(kbd, code))
				goto exit;

			lookup_descriptor(kbd, code, &d, &dl);

			struct cache_entry ce = {
				.code = 0,
				.d = d,
				.dl = dl,
				.layer = 0,
			};
			if (cache_set(kbd, code, &ce))
				goto exit;
		} else {
			struct cache_entry *ce;
			if (!(ce = cache_get(kbd, code)))
				goto exit;

			cache_set(kbd, code, NULL);

			d = ce->d;
			dl = ce->dl;
		}

		process_descriptor(kbd, code, &d, dl, pressed, time);
	}


exit:
	return calculate_main_loop_timeout(kbd, time);
}


int64_t kbd_process_events(struct keyboard *kbd, const struct key_event *events, size_t n, bool real)
{
	assert(kbd->config.finalized);

	size_t i = 0;
	int64_t timeout = 0;
	int64_t timeout_ts = 0;

	while (i != n) {
		const struct key_event *ev = &events[i];
		if (real) {
			kbd->capstate[ev->code] = ev->pressed;
		}

		if (timeout > 0 && timeout_ts <= ev->timestamp) {
			timeout = process_event(kbd, 0, 0, timeout_ts);
			timeout_ts = timeout_ts + timeout;
		} else {
			timeout = process_event(kbd, ev->code, ev->pressed, ev->timestamp);
			timeout_ts = ev->timestamp + timeout;
			i++;
		}
	}

	return timeout;
}

bool kbd_eval(struct keyboard* kbd, std::string_view exp)
{
	if (exp.empty())
		return true;
	if (exp == "reset") {
		kbd->backup->restore(kbd);
		return true;
	}

	if (exp == "unbind_all") {
		// TODO: execute clear? Or it's OK?
		for (auto& layer : kbd->config.layers) {
			layer.chords.clear();
			layer.keymap.mapv.clear();
		}
		return true;
	} else {
		auto section = exp.substr(0, exp.find_first_of(".="));
		if (section.size() == exp.size() || exp[section.size()] != '.')
			section = {};
		else
			exp.remove_prefix(section.size() + 1);
		if (int idx = config_add_entry(&kbd->config, section, exp); idx >= 0) {
			return true;
		}
	}

	return false;
}
