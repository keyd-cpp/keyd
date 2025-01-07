/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>

#include "keyd.h"
#include "ini.h"
#include "keys.h"
#include "log.h"
#include "strutil.h"
#include "unicode.h"
#include <limits>
#include <string>
#include <string_view>
#include <algorithm>

#ifndef DATA_DIR
#define DATA_DIR
#endif

#undef warn
#define warn(fmt, ...) keyd_log("\ty{WARNING:} " fmt "\n", ##__VA_ARGS__)

enum class action_arg_e : signed char {
	ARG_EMPTY,

	ARG_MACRO,
	ARG_LAYER,
	ARG_LAYOUT,
	ARG_TIMEOUT,
	ARG_SENSITIVITY,
	ARG_DESCRIPTOR,
};

using enum action_arg_e;

static struct {
	const char *name;
	const char *preferred_name;
	enum op op;
	enum action_arg_e args[MAX_DESCRIPTOR_ARGS];
} actions[] =  {
	{ "swap", 	NULL,	OP_SWAP,	{ ARG_LAYER } },
	{ "clear", 	NULL,	OP_CLEAR,	{} },
	{ "oneshot", 	NULL,	OP_ONESHOT,	{ ARG_LAYER } },
	{ "toggle", 	NULL,	OP_TOGGLE,	{ ARG_LAYER } },

	{ "clearm", 	NULL,	OP_CLEARM,	{ ARG_MACRO } },
	{ "swapm", 	NULL,	OP_SWAPM,	{ ARG_LAYER, ARG_MACRO } },
	{ "togglem", 	NULL,	OP_TOGGLEM,	{ ARG_LAYER, ARG_MACRO } },
	{ "layerm", 	NULL,	OP_LAYERM,	{ ARG_LAYER, ARG_MACRO } },
	{ "oneshotm", 	NULL,	OP_ONESHOTM,	{ ARG_LAYER, ARG_MACRO } },
	{ "overloadm", nullptr, OP_OVERLOADM, { ARG_LAYER, ARG_MACRO, ARG_DESCRIPTOR } },

	{ "layer", 	NULL,	OP_LAYER,	{ ARG_LAYER } },

	{ "overload", 	NULL,	OP_OVERLOAD,			{ ARG_LAYER, ARG_DESCRIPTOR } },
	{ "overloadt", 	NULL,	OP_OVERLOAD_TIMEOUT,		{ ARG_LAYER, ARG_DESCRIPTOR, ARG_TIMEOUT } },
	{ "overloadt2", NULL,	OP_OVERLOAD_TIMEOUT_TAP,	{ ARG_LAYER, ARG_DESCRIPTOR, ARG_TIMEOUT } },

	{ "overloadi",	NULL,	OP_OVERLOAD_IDLE_TIMEOUT, { ARG_DESCRIPTOR, ARG_DESCRIPTOR, ARG_TIMEOUT } },
	{ "timeout", 	NULL,	OP_TIMEOUT,	{ ARG_DESCRIPTOR, ARG_TIMEOUT, ARG_DESCRIPTOR } },

	{ "macro2", 	NULL,	OP_MACRO2,	{ ARG_TIMEOUT, ARG_TIMEOUT, ARG_MACRO } },
	{ "setlayout", 	NULL,	OP_LAYOUT,	{ ARG_LAYOUT } },

	/* Experimental */
	{ "scrollt", 	NULL,	OP_SCROLL_TOGGLE,		{ARG_SENSITIVITY} },
	{ "scroll", 	NULL,	OP_SCROLL,			{ARG_SENSITIVITY} },

	/* TODO: deprecate */
	{ "overload2", 	"overloadt",	OP_OVERLOAD_TIMEOUT,		{ ARG_LAYER, ARG_DESCRIPTOR, ARG_TIMEOUT } },
	{ "overload3", 	"overloadt2",	OP_OVERLOAD_TIMEOUT_TAP,	{ ARG_LAYER, ARG_DESCRIPTOR, ARG_TIMEOUT } },
	{ "toggle2", 	"togglem",	OP_TOGGLEM,			{ ARG_LAYER, ARG_MACRO } },
	{ "swap2", 	"swapm",	OP_SWAPM,			{ ARG_LAYER, ARG_MACRO } },
};

bool descriptor::operator<(const descriptor& b) const
{
	// This complexity only happens during offline sorting, not matching.
	// Matching uses different logic.
	if (id == b.id) {
		if (mods == b.mods) {
			auto ap = std::popcount(wildcard);
			auto bp = std::popcount(b.wildcard);
			if (ap == bp)
				return wildcard < b.wildcard;
			else
				return ap < bp;
		}
		// Sort by count of bits first
		auto ap = std::popcount(mods);
		auto bp = std::popcount(b.mods);
		if (ap == bp)
			return mods < b.mods;
		else
			return ap < bp;
	}
	return id < b.id;
}

bool descriptor::operator==(const descriptor& b) const
{
	if (id == b.id)
		if (mods == b.mods)
			if (wildcard == b.wildcard)
				return true;
	return false;
}

void descriptor_map::sort()
{
	std::sort(mapv.begin(), mapv.end());
}

void descriptor_map::set(const descriptor& copy, uint32_t hint)
{
	const auto found = std::find(mapv.begin(), mapv.end(), copy);
	if (found != mapv.end()) {
		*found = copy;
		return;
	}

	if (!copy) {
		return;
	}

	mapv.reserve(hint);
	mapv.emplace_back(copy);
}

const descriptor& descriptor_map::operator[](const descriptor& copy) const
{
	// Narrow search range to only key code match
	auto [begin, end] = std::equal_range(mapv.begin(), mapv.end(), copy, [](const descriptor& a, const descriptor& b) {
		return a.id < b.id;
	});

	// Look for exact match first
	for (auto it = begin; it != end; it++) {
		assert(it->id == copy.id);
		if (!it->wildcard && copy.mods == it->mods)
			return *it;
	}

	// Wildcard fallback
	for (auto it = begin; it != end; it++) {
		const uint8_t wc = it->wildcard | it->mods;
		if (it->wildcard && ((wc & copy.mods) ^ copy.mods) == 0) {
			return *it;
		}
	}

	static constexpr descriptor null{};
	return null;
}

static std::string resolve_include_path(const char *path, std::string_view include_path)
{
	std::string resolved_path;
	std::string tmp;

	if (include_path.ends_with(".conf")) {
		warn("%.*s: included file has invalid extension", (int)include_path.size(), include_path.data());
		return {};
	}

	tmp = path;
	resolved_path = dirname(tmp.data());
	resolved_path += '/';
	resolved_path += include_path;

	if (!access(resolved_path.c_str(), F_OK))
		return resolved_path.c_str();

	resolved_path = DATA_DIR "/";
	resolved_path += include_path;

	if (!access(resolved_path.c_str(), F_OK))
		return resolved_path.c_str();

	return resolved_path;
}

static std::string read_file(const char *path, size_t recursion_depth = 0)
{
	std::string buf;

	std::string file = file_reader(open(path, O_RDONLY), 512, [&] {
		err("failed to open %s", path);
		perror("open");
	});

	for (auto line : split_char<'\n'>(file)) {
		if (line.starts_with("include ") || line.starts_with("include\t")) {
			auto include_path = line.substr(8);

			std::string resolved_path = resolve_include_path(path, include_path);
			if (resolved_path.empty()) {
				warn("failed to resolve include path: %.*s", (int)include_path.size(), include_path.data());
				continue;
			}

			if (recursion_depth >= 10) {
				warn("include depth too big or cyclic: %.*s", (int)include_path.size(), include_path.data());
				continue;
			}

			buf += read_file(resolved_path.c_str(), recursion_depth + 1);
		} else {
			buf.append(line);
			buf += '\n';
		}
	}

	return buf;
}


/* Return descriptor with keycode and parse mods (partial success possible). */
static descriptor lookup_keycode(std::string_view name)
{
	descriptor r{};
	if (auto res = parse_key_sequence(name, &r.args[0].code, &r.args[1].mods, &r.args[2].wildc); res < 0) {
		r.op = OP_NULL;
		return r;
	} else {
		name = name.substr(name.size() - res);
		r.op = OP_KEYSEQUENCE;
		static constexpr auto add = KEYD_ENTRY_COUNT - KEYD_FAKEMOD;
		if (name == "control" || name == "ctrl")
			r.id = KEYD_FAKEMOD_CTRL + add;
		else if (name == "shift")
			r.id = KEYD_FAKEMOD_SHIFT + add;
		else if (name == "alt")
			r.id = KEYD_FAKEMOD_ALT + add;
		else if (name == "altgr")
			r.id = KEYD_FAKEMOD_ALTGR + add;
		else if (name == "meta" || name == "super")
			r.id = KEYD_FAKEMOD_SUPER + add;
		else if (name == "hyper")
			r.id = KEYD_FAKEMOD_HYPER + add;
		else if (name == "level5")
			r.id = KEYD_FAKEMOD_LEVEL5 + add;
		else if (name == "mod7" || name == "nlock")
			r.id = KEYD_FAKEMOD_NUMLOCK + add;
		else
			r.id = r.args[0].code;
		r.mods = r.args[1].mods;
		r.wildcard = r.args[2].wildc;
		// Allow partial failure with zero keycode
		if (r.id == 0)
			r.op = OP_NULL;
		return r;
	}
}

static struct descriptor *layer_lookup_chord(struct layer *layer, decltype(chord::keys)& keys, size_t n)
{
	for (size_t i = 0; i < layer->chords.size(); i++) {
		size_t nm = 0;
		struct chord *chord = &layer->chords[i];

		for (size_t j = 0; j < n; j++) {
			for (size_t k = 0; k < chord->keys.size(); k++)
				if (keys[j] == chord->keys[k]) {
					nm++;
					break;
				}
		}

		if (nm == n)
			return &chord->d;
	}

	return NULL;
}

static uint8_t get_mods(long idx)
{
	if (idx == 0)
		return 0;
	if (idx <= MAX_MOD)
		return (1 << (idx - 1));
	return 0;
}

static uint8_t get_mods(const struct config* cfg, const struct layer* layer)
{
	if (layer->name[0])
		return get_mods(layer - cfg->layers.data());
	uint8_t r = 0;
	for (uint16_t idx : *layer) {
		r |= get_mods(idx);
	}
	return r;
}

/*
 * Consumes a string of the form `[<layer>.]<key> = <descriptor>` and adds the
 * mapping to the corresponding layer in the config.
 */

static int set_layer_entry(const struct config *config,
			   struct layer *layer, char *key,
			   const struct descriptor *d)
{
	if (strchr(key, '+')) {
		//TODO: Handle aliases
		//TODO: what do to with modifiers?
		char *tok;
		struct descriptor *ld;
		decltype(chord::keys) keys{};
		size_t n = 0;

		for (tok = strtok(key, "+"); tok; tok = strtok(NULL, "+")) {
			descriptor desc = lookup_keycode(tok);
			if (!desc || desc.mods || desc.wildcard) {
				err("%s is not a valid key", tok);
				return -1;
			}

			for (size_t i = 0; i < MAX_MOD; i++) {
				if (config->modifiers[i].find_first_of(desc.id) + 1) {
					desc.id = KEYD_ENTRY_COUNT + i;
					break;
				}
			}

			if (desc.id >= KEYD_ENTRY_COUNT) {
				err("chord key %s+ is a modifier, did you mean to use %c-key combo?", tok, mod_ids.at(desc.id - KEYD_ENTRY_COUNT));
				return -1;
			}

			if (n >= ARRAY_SIZE(keys)) {
				err("chords cannot contain more than %ld keys", n);
				return -1;
			}

			keys[n++] = desc.id;
		}


		if ((ld = layer_lookup_chord(layer, keys, n))) {
			*ld = *d;
		} else {
			layer->chords.emplace_back() = {keys, *d};
		}
	} else {
		std::string_view expr = key;
		expr.remove_prefix(expr.find_last_of("-*") + 1);
		auto found = config->aliases.find(expr);
		if (found != config->aliases.end()) {
			// Lookup mods
			descriptor aux = lookup_keycode(key);
			descriptor desc = *d;
			for (const auto& alias : found->second) {
				if (alias.op != OP_KEYSEQUENCE)
					continue;
				desc.id = alias.id;
				desc.mods = aux.mods | alias.mods | get_mods(config, layer);
				desc.mods |= config->add_left_mods;
				desc.wildcard = aux.wildcard | alias.wildcard;
				desc.wildcard |= config->add_left_wildc;
				if (config->compat)
					desc.wildcard = -1;
				desc.wildcard &= ~desc.mods;
				if (desc.id >= KEYD_ENTRY_COUNT) {
					for (uint16_t id : config->modifiers.at(desc.id - KEYD_ENTRY_COUNT)) {
						desc.id = id;
						layer->keymap.set(desc);
					}
				} else {
					layer->keymap.set(desc);
				}
			}
		} else {
			descriptor desc = lookup_keycode(key);
			if (!desc) {
				err("%s is not a valid key or alias", key);
				return -1;
			}
			desc.op = d->op;
			desc.args = d->args;
			desc.wildcard |= config->add_left_wildc;
			if (config->compat)
				desc.wildcard = -1; // Something resembling backwards compatibility
			desc.mods |= get_mods(config, layer); // Combine (automatically add left-hand mods)
			desc.mods |= config->add_left_mods;
			desc.wildcard &= ~desc.mods; // Wildcard priority adjustment
			if (desc.id >= KEYD_ENTRY_COUNT) {
				for (uint16_t id : config->modifiers.at(desc.id - KEYD_ENTRY_COUNT)) {
					desc.id = id;
					layer->keymap.set(desc);
				}
			} else {
				layer->keymap.set(desc);
			}
		}
	}

	return 0;
}

static std::pair<std::string, uint16_t> layer_composition(struct config* config, std::string_view str)
{
	std::u16string arr;
	std::string result;
	for (auto name : split_char<'+'>(str)) {
		// Disable weird stuff like "a++b"
		if (name.empty())
			return {};
		// Remove tautological inclusion of main
		if (name == config->layers[0].name)
			continue;
		uint16_t idx = 0;
		// Fix name variants
		if (name == "ctrl")
			name = "control";
		if (name == "super")
			name = "meta";
		if (name == "nlock")
			name = "mod7";
		for (size_t i = 1; i <= MAX_MOD; i++) {
			if (name == config->layers[i].name) {
				idx = i;
				break;
			}
		}
		// Possibly create new singular layer (ignore layer limit for now)
		if (!idx) {
			auto it = std::lower_bound(config->layer_index.begin(), config->layer_index.end(), 0, [&](uint16_t a, int) {
				if (config->layers[a].size() == 1) {
					return config->layers[a].name < name;
				}
				return false;
			});
			if (it == config->layer_index.end() || config->layers[*it].name != name) {
				idx = config->layers.size();
				config->layer_index.insert(it, idx);
				config->layers.emplace_back().name = name;
			} else {
				idx = *it;
			}
		}
		arr += char16_t(idx);
	}
	std::sort(arr.begin(), arr.end());
	arr.erase(std::unique(arr.begin(), arr.end()), arr.end());
	// Pack constitutients into name, because why not
	result.push_back('\0');
	result.insert(1, reinterpret_cast<char*>(arr.data()), arr.size() * 2);
	return std::make_pair(std::move(result), arr[0]);
}

/*
 * Returns:
 * 	Layer index if exists or created
 * 	< 0 on error
 */
static int config_access_layer(struct config *config, std::string_view name, bool singular = false)
{
	if (name.empty()) [[unlikely]]
		return -1;
	// [+] = shortcut for [main]
	if (name.find_first_not_of('+') == size_t(-1))
		return 0;

	auto&& [compose, single] = layer_composition(config, name.substr(0, name.find_first_of(":")));
	if (compose.empty())
		return -1;
	// Return simple layer
	if (compose.size() / 2 <= 1)
		return single;
	if (singular)
		return -1;
	auto it = std::lower_bound(config->layer_index.begin(), config->layer_index.end(), 0, [&](uint16_t a, int) {
		if (config->layers[a].size() == compose.size() / 2) {
			return config->layers[a].name < compose;
		}
		return config->layers[a].size() < compose.size() / 2;
	});
	// Return existing layer
	if (it != config->layer_index.end() && config->layers[*it].name == compose) {
		return *it;
	}

	size_t idx = config->layers.size();
	if (idx > INT16_MAX) {
		err("max layers exceeded");
		return -1;
	}
	// New composite layer
	config->layer_index.insert(it, idx);
	config->layers.emplace_back().name = std::move(compose);
	return idx;
}

/* Modifies the input string */
static int parse_fn(char *s,
		    char **name,
		    char *args[5],
		    size_t *nargs)
{
	char *c, *arg;

	c = s;
	while (*c && *c != '(')
		c++;

	if (!*c)
		return -1;

	*name = s;
	*c++ = 0;

	while (*c == ' ')
		c++;

	*nargs = 0;
	arg = c;
	while (1) {
		int plvl = 0;

		while (*c) {
			switch (*c) {
			case '\\':
				if (*(c+1)) {
					c+=2;
					continue;
				}
				break;
			case '(':
				plvl++;
				break;
			case ')':
				plvl--;

				if (plvl == -1)
					goto exit;
				break;
			case ',':
				if (plvl == 0)
					goto exit;
				break;
			}

			c++;
		}
exit:

		if (!*c)
			return -1;

		if (arg != c) {
			assert(*nargs < 5);
			args[(*nargs)++] = arg;
		}

		if (*c == ')') {
			*c = 0;
			return 0;
		}

		*c++ = 0;
		while (*c == ' ')
			c++;
		arg = c;
	}
}

/*
 * Parses macro expression placing the result
 * in the supplied macro struct.
 *
 * Returns:
 *   0 on success
 *   -1 in the case of an invalid macro expression
 *   > 0 for all other errors
 */

static int parse_macro_expression(std::string_view s, macro& macro, struct config* config, uint8_t* wildcard)
{
	uint8_t mods;
	uint16_t code;
	auto res = parse_key_sequence(s, &code, &mods, wildcard);
	if (res < 0) {
		return res;
	}
	if (config->compat)
		*wildcard = -1;
	*wildcard |= config->add_right_wildc;
	if (res == 0) {
		// Section modifiers are not active inside the macro itself
		mods |= config->add_right_mods;
		*wildcard |= mods;
		macro.size = 1;
		macro.entry = macro_entry{
			.type = MACRO_KEYSEQUENCE,
			.id = code,
			.mods = { .mods = mods, .wildc = *wildcard },
		};
		return 0;
	}
	if (size_t(res) < s.size() && *wildcard != 0xff) {
		err("Invalid macro prefix (only ** is supported): %.*s\n", (int)s.size(), s.data());
		return -1;
	}
	s = s.substr(s.size() - res);
	if (s.starts_with("macro(") && s.ends_with(')')) {
		s.remove_suffix(1);
		s.remove_prefix(6);
	} else if (s.ends_with(')') && (s.starts_with("type(") || s.starts_with("text(") || s.starts_with("t(") || s.starts_with("txt("))) {
		// Pass to macro_parse as is
	} else if ((s.starts_with("cmd(") || s.starts_with("command(")) && s.ends_with(')')) {
		// Same
	} else if (utf8_strlen(s) != 1) {
		err("Invalid macro: %.*s\n", (int)s.size(), s.data());
		return -1;
	} else {
		warn("Naked unicode is deprecated, use type(): %.*s", (int)s.size(), s.data());
	}

	return macro_parse(s, macro, config) == 0 ? 0 : 1;
}

static int parse_descriptor(char *s,
			    struct descriptor *d,
			    struct config *config)
{
	char *fn = NULL;
	char *args[5];
	size_t nargs = 0;
	uint16_t code;
	uint8_t mods;
	uint8_t wildc;
	int ret;
	::macro macro;
	std::string cmd;

	if (!s || !s[0]) {
		d->op = OP_NULL;
		return 0;
	}

	if (parse_key_sequence(s, &code, &mods, &wildc) == 0) {
		if (config->compat)
			wildc = -1;
		d->op = OP_KEYSEQUENCE;
		d->args[0].code = code;
		d->args[1].mods = mods | config->add_right_mods;
		d->args[2].wildc = wildc | config->add_right_wildc;
		return 0;
	} else if ((ret = parse_macro_expression(s, macro, config, &wildc)) >= 0) {
		if (ret)
			return -1;

		if (config->macros.size() >= INT16_MAX) {
			err("max macros exceeded");
			return -1;
		}

		d->op = OP_MACRO;
		d->args[0].code = config->macros.size() | (wildc ? 0x8000 : 0);
		config->macros.emplace_back(std::move(macro));

		return 0;
	} else if (!parse_fn(s, &fn, args, &nargs)) {
		int i;

		if (!strcmp(fn, "lettermod")) {
			std::string buf;

			if (nargs != 4) {
				err("%s requires 4 arguments", fn);
				return -1;
			}

			buf = buf + "overloadi(" + args[1] + ", overloadt2(" + args[0] + ", " + args[1] + ", " + args[3] + "), " + args[2] + ")";
			if (parse_fn(buf.data(), &fn, args, &nargs)) {
				err("failed to parse %s", buf.c_str());
				return -1;
			}
		}

		for (i = 0; i < ARRAY_SIZE(actions); i++) {
			if (!strcmp(actions[i].name, fn)) {
				int j;

				if (actions[i].preferred_name)
					warn("%s is deprecated (renamed to %s).", actions[i].name, actions[i].preferred_name);

				d->op = actions[i].op;

				for (j = 0; j < MAX_DESCRIPTOR_ARGS; j++) {
					if (actions[i].args[j] == ARG_EMPTY)
						break;
				}

				if ((int)nargs != j) {
					err("%s requires %d %s", actions[i].name, j, j == 1 ? "argument" : "arguments");
					return -1;
				}

				while (j--) {
					auto type = actions[i].args[j];
					union descriptor_arg *arg = &d->args[j];
					char *argstr = args[j];
					struct descriptor desc{};

					switch (type) {
					case ARG_LAYER:
						if (argstr[0] == '+' && !argstr[1]) {
							// Special value
							arg->idx = 0;
						} else if (argstr[0] == '*' && argstr[1] == '*' && !argstr[2]) {
							// Same, since ** are everywhere
							arg->idx = 0;
						} else if (argstr[0] == '-' && !argstr[1]) {
							// Special value: no layer
							arg->idx = INT16_MIN;
						} else {
							arg->idx = config_access_layer(config, argstr + (argstr[0] == '-' && argstr[1]));
							if (arg->idx <= 0) {
								err("%s layer cannot be used", argstr);
								return -1;
							}
						}

						// Layer subtraction (experimental)
						if (argstr[0] == '-' && arg->idx != INT16_MIN)
							arg->idx = -arg->idx;
						break;
					case ARG_LAYOUT:
						arg->idx = config_access_layer(config, argstr, true);
						if (arg->idx == -1) {
							err("%s layout cannot be used", argstr);
							return -1;
						}

						break;
					case ARG_DESCRIPTOR:
						if (parse_descriptor(argstr, &desc, config))
							return -1;

						if (config->descriptors.size() > INT16_MAX) {
							err("maximum descriptors exceeded");
							return -1;
						}

						arg->idx = config->descriptors.size();
						config->descriptors.emplace_back(std::move(desc));
						break;
					case ARG_SENSITIVITY:
						arg->sensitivity = atoi(argstr);
						break;
					case ARG_TIMEOUT:
						arg->timeout = atoi(argstr);
						break;
					case ARG_MACRO:
						if (config->macros.size() > INT16_MAX) {
							err("max macros exceeded");
							return -1;
						}

						config->macros.emplace_back();
						if (parse_macro_expression(argstr, config->macros.back(), config, &wildc)) {
							config->macros.pop_back();
							return -1;
						}

						arg->code = (config->macros.size() - 1) | (wildc ? 0x8000 : 0);
						break;
					default:
						assert(0);
						break;
					}
				}

				return 0;
			}
		}
	}

	err("invalid key or action: %s", s);
	return -1;
}

static void parse_global_section(struct config *config, struct ini_section *section)
{
	for (size_t i = 0; i < section->entries.size(); i++) {
		struct ini_entry *ent = &section->entries[i];

		if (!strcmp(ent->key, "macro_timeout"))
			config->macro_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "macro_sequence_timeout"))
			config->macro_sequence_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "disable_modifier_guard"))
			config->disable_modifier_guard = atoi(ent->val);
		else if (!strcmp(ent->key, "oneshot_timeout"))
			config->oneshot_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "chord_hold_timeout"))
			config->chord_hold_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "chord_timeout"))
			config->chord_interkey_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "default_layout"))
			config->default_layout = ent->val;
		else if (!strcmp(ent->key, "macro_repeat_timeout"))
			config->macro_repeat_timeout = atoi(ent->val);
		else if (!strcmp(ent->key, "layer_indicator"))
			config->layer_indicator = atoi(ent->val);
		else if (!strcmp(ent->key, "overload_tap_timeout"))
			config->overload_tap_timeout = atoi(ent->val);
		else
			warn("line %zd: %s is not a valid global option", ent->lnum, ent->key);
	}
}

static void parse_id_section(struct config *config, struct ini_section *section)
{
	for (size_t i = 0; i < section->entries.size(); i++) {
		uint16_t product, vendor;

		struct ini_entry *ent = &section->entries[i];
		std::string_view s = ent->key;

		if (s.starts_with('*')) {
			warn("Use k:* to capture keyboards. Wildcard compat mode enabled.");
			config->compat = 1;
		} else if (s.starts_with("m:*")) {
			config->wildcard |= CAP_MOUSE;
		} else if (s.starts_with("k:*")) {
			config->wildcard |= CAP_KEYBOARD;
		} else if (s.starts_with("a:*")) {
			config->wildcard |= CAP_MOUSE_ABS;
		}
		continue;

		if ((s.starts_with("m:") || s.starts_with("a:")) && s.size() - 2 <= sizeof(dev_id::id) - 3) {
			config->ids.push_back({
				.flags = ID_MOUSE,
				.id = {}
			});
			if (s[0] == 'a')
				config->ids.back().flags |= ID_ABS_PTR;
			s.remove_prefix(2);
		} else if (s.starts_with("k:") && s.size() - 2 <= sizeof(dev_id::id) - 3) {
			config->ids.push_back({
				.flags = ID_KEYBOARD,
				.id = {}
			});
			s.remove_prefix(2);
		} else if (s.starts_with('-') && s.size() - 1 <= sizeof(dev_id::id) - 2) {
			config->ids.push_back({
				.flags = ID_EXCLUDED,
				.id = {}
			});
			s.remove_prefix(1);
		} else if (s.size() - 0 < sizeof(dev_id::id) - 1) {
			config->ids.push_back({
				.flags = ID_KEYBOARD | ID_MOUSE,
				.id = {}
			});
		} else {
			warn("%.*s is not a valid device id", (int)s.size(), s.data());
			continue;
		}

		memcpy(config->ids.back().id.data(), s.data(), s.size());
	}
}

static void parse_alias_section(struct config *config, struct ini_section *section)
{
	for (size_t i = 0; i < section->entries.size(); i++) {
		struct ini_entry *ent = &section->entries[i];
		const char *name = ent->val;

		if (auto desc = lookup_keycode(ent->key)) {
			if (name && !desc.mods && !desc.wildcard && desc.id < KEYD_ENTRY_COUNT && name[0] && !name[1]) {
				// Add modifier keys
				if (size_t id = mod_ids.find_first_of(name[0]); id + 1 || name == "-"sv) {
					// Remove this key from any other modifiers
					for (auto& mods : config->modifiers)
						std::erase(mods, desc.id);
					if (id + 1)
						config->modifiers[id].push_back(desc.id);
					continue;
				}
			}
			if (name) {
				// TODO: check possibly incorrect names
				auto alias = lookup_keycode(name);
				if (alias) {
					warn("alias name represents a valid keycode: %s", name);
				} else {
					if (alias.wildcard)
						warn("alias contains wildcard, ignored: %s", name);
					config->aliases[name].emplace_back(desc);
				}
			}
		} else {
			warn("failed to define alias %s, %s is not a valid keycode", name, ent->key);
		}
	}
}


static int config_parse_string(struct config *config, char *content)
{
	size_t i;
	::ini ini = ini_parse_string(content, NULL);
	if (ini.empty())
		return -1;

	/* First pass: create all layers based on section headers.  */
	for (i = 0; i < ini.size(); i++) {
		struct ini_section *section = &ini[i];

		if (section->name == "ids") {
			parse_id_section(config, section);
		} else if (section->name == "aliases") {
			parse_alias_section(config, section);
		} else if (section->name == "global") {
			parse_global_section(config, section);
		} else {
			continue;
		}

		section->entries = {};
	}

	/* Populate each layer. */
	for (i = 0; i < ini.size(); i++) {
		struct ini_section *section = &ini[i];
		std::string_view name = section->name;
		name = name.substr(0, name.find_first_of(':'));
		if (name.size() != section->name.size())
			warn("obsolete layer type specifier: %s", section->name.c_str());

		if (section->entries.empty())
			continue;
		// Parse section-specific modifiers
		config->add_right_wildc = 0;
		config->add_right_mods = 0;
		config->add_left_wildc = 0;
		config->add_left_mods = 0;

		while (name.size() >= 2) {
			if (name.ends_with("**"))
				config->add_right_wildc = -1;
			else if (auto pos = mod_ids.find_first_of(name.back()); pos + 1 && name[name.size() - 2] == '*')
				config->add_right_wildc |= 1 << pos;
			else if (pos + 1 && name[name.size() - 2] == '-')
				config->add_right_mods |= 1 << pos;
			else
				break;
			name.remove_suffix(2);
		}

		while (name.size() >= 2) {
			if (name.starts_with("**"))
				config->add_left_wildc = -1;
			else if (auto pos = mod_ids.find_first_of(name.front()); pos + 1 && name[1] == '-')
				config->add_left_mods |= 1 << pos;
			else if (pos + 1 && name[1] == '*')
				config->add_left_wildc |= 1 << pos;
			else
				break;
			name.remove_prefix(2);
		}

		for (size_t j = 0; j < section->entries.size(); j++) {
			struct ini_entry *ent = &section->entries[j];
			if (!ent->val) {
				warn("invalid binding on line %zd", ent->lnum);
				continue;
			}

			std::string entry(name);
			entry += ".";
			entry += ent->key;
			entry += " = ";
			entry += ent->val;
			if (config_add_entry(config, entry) < 0)
				keyd_log("\tr{ERROR:} line m{%zd}: %s\n", ent->lnum, errstr);
		}

		section->entries = {};
	}

	config->add_right_wildc = 0;
	config->add_right_mods = 0;
	config->add_left_wildc = 0;
	config->add_left_mods = 0;

	for (auto& layer : config->layers)
		layer.keymap.sort();
	return 0;
}

static void config_init(struct config *config)
{
	if (config->layers.empty()) {
		// Populate special layers
		config->layers.emplace_back().name = "main";
		config->layers.emplace_back().name = "alt";
		config->layers.emplace_back().name = "meta";
		config->layers.emplace_back().name = "shift";
		config->layers.emplace_back().name = "control";
		config->layers.emplace_back().name = "altgr";
		config->layers.emplace_back().name = "hyper";
		config->layers.emplace_back().name = "level5";
		config->layers.emplace_back().name = "mod7";
	}

	char default_config[] =
	"[aliases]\n"

	"leftshift = S\n"
	"rightshift = S\n"
	"leftalt = A\n"
	"rightalt = G\n"
	"leftmeta = M\n"
	"rightmeta = M\n"
	"leftctrl = C\n"
	"rightctrl = C\n"

	// Need to set H/L/N to use remaining inactive mods
	"\n";

	config_parse_string(config, default_config);

	/* In ms */
	config->chord_interkey_timeout = 50;
	config->chord_hold_timeout = 0;
	config->oneshot_timeout = 0;

	config->macro_timeout = 600;
	config->macro_repeat_timeout = 50;
}

int config_parse(struct config *config, const char *path)
{
	std::string content = read_file(path);
	if (content.empty())
		return -1;

	config_init(config);
	config->pathstr = path;
	return config_parse_string(config, content.data());
}

int config_check_match(struct config *config, const char *id, uint8_t flags)
{
	for (size_t i = 0; i < config->ids.size(); i++) {
		//Prefix match to allow matching <product>:<vendor> for backward compatibility.
		if (strstr(id, config->ids[i].id.data()) == id) {
			if (config->ids[i].flags & ID_EXCLUDED) {
				return 0;
			} else if (config->ids[i].flags & flags) {
				if ((flags & ID_ABS_PTR) && (~config->ids[i].flags & ID_ABS_PTR))
					continue;
				return 2;
			}
		}
	}

	// Wildcard match
	if ((config->wildcard & CAP_KEYBOARD) && (flags & ID_KEYBOARD))
		return 1;
	if ((config->wildcard & CAP_MOUSE) && (flags & ID_MOUSE) && (~flags & ID_ABS_PTR))
		return 1;
	if ((config->wildcard & CAP_MOUSE_ABS) && (flags & ID_ABS_PTR))
		return 1;

	return 0;
}

/*
 * Adds a binding of the form [<layer>.]<key> = <descriptor expression>
 * to the given config. Returns layer index that was modified.
 */
int config_add_entry(struct config* config, std::string_view exp)
{
	char *keyname, *descstr, *dot, *paren, *s;
	const char *layername = config->layers[0].name.c_str();
	struct descriptor d;

	static std::string buf;
	buf.assign(exp);
	s = buf.data();

	dot = strchr(s, '.');
	paren = strchr(s, '(');

	if (dot && dot != s && (!paren || dot < paren)) {
		layername = s;
		*dot = 0;
		s = dot+1;
	}

	parse_kvp(s, &keyname, &descstr);
	int idx = config_access_layer(config, layername);
	if (idx == -1) {
		err("%s is not a valid layer", layername);
		return -1;
	}

	if (parse_descriptor(descstr, &d, config) < 0)
		return -1;

	if (set_layer_entry(config, &config->layers[idx], keyname, &d) < 0)
		return -1;

	return idx;
}

const char* env_pack::getenv(std::string_view name)
{
	if (!env)
		return nullptr;
	for (size_t i = 0;; i++){
		const char* ptr = env[i];
		if (!ptr)
			return nullptr;
		// TODO: is this safe?
		if (!strncmp(ptr, name.data(), name.size())) {
			ptr += name.size();
			if (*ptr == '=')
				return ptr + 1;
			if (*ptr == 0)
				return nullptr;
		}
	}
}

config_backup::config_backup(const struct config& cfg)
	: descriptor_count(cfg.descriptors.size())
	, macro_count(cfg.macros.size())
	, cmd_count(cfg.macros.size())
	, layers(cfg.layers.size())
	, mods(cfg.modifiers)
{
	for (size_t i = 0; i < layers.size(); i++) {
		layers[i] = {
			.keymap = cfg.layers[i].keymap,
			.chords = cfg.layers[i].chords,
		};
	}
}

void config_backup::restore(struct keyboard* kbd)
{
	::config& cfg = kbd->config;
	for (size_t i = 0; i < layers.size(); i++) {
		auto& layer = cfg.layers[i];
		if (true /* modified */) {
			layer.chords = layers[i].chords;
			layer.keymap = layers[i].keymap;
		}
	}
	std::erase_if(cfg.layer_index, [&](uint16_t idx) {
		return idx >= layers.size();
	});
	cfg.layers.resize(layers.size());
	cfg.descriptors.resize(descriptor_count);
	cfg.macros.resize(macro_count);
	cfg.commands.resize(cmd_count);
	cfg.modifiers = this->mods;
}

config::~config()
{
}

config_backup::~config_backup()
{
}
