/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <libgen.h>

#include "keyd.h"
#include "keys.h"
#include "log.h"
#include "strutil.h"
#include "unicode.h"
#include <limits>
#include <string>
#include <string_view>
#include <algorithm>
#include <numeric>
#include <charconv>

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

constexpr struct {
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

[[gnu::noinline]] bool descriptor::equals(const struct config* cfg, const descriptor& b) const
{
	// Deep descriptor comparison
	// Shouldn't normally be called unless descriptor match conflicts occur
	// It could be used to deduplicate entries at config parse phase but it may be too slow?
	if (op == OP_NULL || !b || op != b.op || *this != b)
		return false;
	if (op == OP_KEYSEQUENCE) {
		return args[0].code == b.args[0].code && args[1].mods == b.args[1].mods && args[2].wildc == b.args[2].wildc;
	}
	if (op == OP_MACRO) {
		if ((args[0].code & 0x8000) != (b.args[0].code & 0x8000))
			return false;
		if (!cfg->macros[args[0].code & 0x7fff].equals(cfg, cfg->macros[b.args[0].code & 0x7fff]))
			return false;
		return true;
	}
	// clang++-18 actually unrolls this into switch, no loops
	#pragma GCC unroll 999
	for (auto [n1, n2, act, types] : actions) {
		if (op == act) {
			for (int i = 0; i < MAX_DESCRIPTOR_ARGS; i++) {
				switch (types[i]) {
				case ARG_EMPTY:
					continue;
				case ARG_LAYER:
				case ARG_LAYOUT:
				case ARG_TIMEOUT:
				case ARG_SENSITIVITY:
					if (memcmp(&args[i], &b.args[i], sizeof(args[0])) != 0)
						return false;
					continue;
				case ARG_MACRO:
					if ((args[i].code & 0x8000) != (b.args[i].code & 0x8000))
						return false;
					if (!cfg->macros[args[i].code & 0x7fff].equals(cfg, cfg->macros[b.args[i].code & 0x7fff]))
						return false;
					continue;
				case ARG_DESCRIPTOR:
					if (!cfg->descriptors[args[i].idx].equals(cfg, cfg->descriptors[b.args[i].idx]))
						return false;
					continue;
				}
			}
			return true;
		}
	}

	die("%s: unhandled op", __FUNCTION__);
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

// Callback: path, line number, line
static bool read_ini_file(const char* path, size_t max_depth, auto&& cb)
{
	file_mapper file(open(path, O_RDONLY));
	if (!file) {
		keyd_log("Unable to open %s\n", path);
		return false;
	}

	std::size_t nline = 0;

	for (auto line : split_char<'\n'>(file.view())) {
		if (line.starts_with("include ") || line.starts_with("include\t")) {
			auto include_path = line.substr(8);

			std::string resolved_path = resolve_include_path(path, include_path);
			if (resolved_path.empty()) {
				warn("failed to resolve include path: %.*s", (int)include_path.size(), include_path.data());
				continue;
			}

			if (!max_depth) {
				warn("include depth too big or cyclic: %.*s", (int)include_path.size(), include_path.data());
				continue;
			}

			read_ini_file(resolved_path.c_str(), max_depth - 1, std::move(cb));
		} else {
			// Filter spaces and comments
			line = line.substr(std::min(line.size(), line.find_first_not_of(C_SPACES)));
			line = line.substr(0, line.find_last_not_of(C_SPACES) + 1);
			if (!line.empty() && !line.starts_with('#'))
				cb(path, nline, line);
		}
		nline++;
	}

	return true;
}

// Return value string from ' = value' (with key already parsed)
static std::string_view get_ini_value(std::string_view s)
{
	s = s.substr(std::min(s.size(), s.find_first_not_of(C_SPACES)));
	if (!s.starts_with('='))
		return {};
	s.remove_prefix(1);
	return s.substr(std::min(s.size(), s.find_first_not_of(C_SPACES)));
}

template <typename T>
bool parse_int(std::string_view name, T& value, std::string_view s,
	std::type_identity_t<T> min = std::numeric_limits<T>::min(),
	std::type_identity_t<T> max = std::numeric_limits<T>::max())
{
	if (s.starts_with(name))
		s.remove_prefix(name.size());
	else
		return false;
	s = get_ini_value(s);
	if (s.empty())
		return false;
	T tmp{};
	auto r = std::from_chars(s.data(), s.data() + s.size(), tmp);
	// String tail is not checked as a form of backward compat
	if (r.ec != std::errc())
		return false;
	if (tmp < min || tmp > max)
		return false;
	value = tmp;
	return true;
}

/* Return descriptor with keycode and parse mods (partial success possible). */
static std::pair<descriptor, std::string_view> lookup_keycode(std::string_view s)
{
	descriptor r{};
	if (auto res = parse_key_sequence(s, &r.args[0].code, &r.args[1].mods, &r.args[2].wildc); res < 0) {
		r.op = OP_NULL;
		return {r, {}};
	} else {
		s = s.substr(s.size() - res);
		std::string_view name = s.substr(0, s.find_first_of(C_SPACES "="));
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
			r.id = KEYD_FAKEMOD_NLOCK + add;
		else
			r.id = r.args[0].code;
		if (r.id >= KEYD_ENTRY_COUNT)
			s.remove_prefix(name.size());
		r.mods = r.args[1].mods;
		r.wildcard = r.args[2].wildc;
		// Allow partial failure with zero keycode
		if (r.id == 0)
			r.op = OP_NULL;
		return {r, s};
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

static int parse_descriptor(std::string_view s, struct descriptor *d, struct config *config);

static bool set_layer_entry(struct config *config, int16_t idx, std::string_view s)
{
	struct chord chord{};
	size_t n = 0;

	struct descriptor dd{};
	struct descriptor* d = &dd;

	while (true) {
		auto [desc, next] = lookup_keycode(s);
		if (n || next.starts_with('+')) {
			if (!desc || desc.mods || desc.wildcard) {
				err("%.*s is not a valid chord key", (int)s.size(), s.data());
				return -1;
			}

			for (size_t i = 0; i < MAX_MOD; i++) {
				if (config->modifiers[i].find_first_of(desc.id) + 1) {
					desc.id = KEYD_ENTRY_COUNT + i;
					break;
				}
			}

			if (desc.id >= KEYD_ENTRY_COUNT) {
				err("chord key %.*s+ is a modifier, did you mean to use %c-key combo?", (int)s.size(), s.data(), mod_ids.at(desc.id - KEYD_ENTRY_COUNT));
				return -1;
			}

			if (n >= ARRAY_SIZE(chord.keys)) {
				err("chords cannot contain more than %zu keys", n);
				return -1;
			}

			chord.keys[n++] = desc.id;

			if (next.starts_with('+')) {
				s = next;
				s.remove_prefix(1);
				continue;
			}
		}

		if (n) {
			if (parse_descriptor(get_ini_value(next), d, config) < 0)
				return false;

			if (!(d = layer_lookup_chord(&config->layers[idx], chord.keys, n))) {
				config->layers[idx].chords.emplace_back(chord).d = dd;
			} else {
				*d = dd;
			}
			return true;
		}

		// Get alias name
		std::string_view aname = next.substr(0, next.find_first_of(C_SPACES "="));
		next.remove_prefix(aname.size());

		if (parse_descriptor(get_ini_value(next), d, config) < 0)
			return false;

		// parse_descriptor can create layers, so use idx
		struct layer* layer = &config->layers[idx];
		auto found = config->aliases.find(aname);
		if (found != config->aliases.end()) {
			// Lookup mods, use key descriptor as aux, and desc as value descriptor
			descriptor aux = desc;
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
			if (!desc) {
				err("%.*s is not a valid key or alias (%.*s)", (int)s.size(), s.data(), (int)next.size(), next.data());
				return false;
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

		break;
	}

	return true;
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
	if (name.empty() || name.find_first_of('.') + 1) [[unlikely]]
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
		// Try to make a macro that is like a real keysequence
		// Because it's written like one in this case
		mods |= config->add_right_mods;
		*wildcard |= mods;
		macro.size = 1;
		macro.entry = macro_entry{
			.type = MACRO_KEY_SEQ,
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

static int parse_descriptor(std::string_view s, struct descriptor *d, struct config *config)
{
	char *fn = NULL;
	char *args[5];
	size_t nargs = 0;
	uint16_t code;
	uint8_t mods;
	uint8_t wildc;
	int ret;
	::macro macro;

	if (s.empty()) {
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
	} else if (std::string sbuf{s}, buf; !parse_fn(sbuf.data(), &fn, args, &nargs)) {
		int i;

		if (!strcmp(fn, "lettermod")) {
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

	err("invalid key or action: %.*s", (int)s.size(), s.data());
	return -1;
}

static void parse_global_section(struct config* config, const char* file, size_t ln, std::string_view s)
{
	if (parse_int("macro_timeout", config->macro_timeout, s, 0))
		return;
	else if (parse_int("macro_sequence_timeout", config->macro_sequence_timeout, s, 0))
		return;
	else if (parse_int("disable_modifier_guard", config->disable_modifier_guard, s, 0, 1))
		return;
	else if (parse_int("oneshot_timeout", config->oneshot_timeout, s, 0))
		return;
	else if (parse_int("chord_hold_timeout", config->chord_hold_timeout, s, 0))
		return;
	else if (parse_int("chord_timeout", config->chord_interkey_timeout, s, 0))
		return;
	else if (s.starts_with("default_layout") && s.find_first_of(C_SPACES "=") == 14)
		return config->default_layout = s.substr(s.find_last_of(C_SPACES "=") + 1), void(); // TODO
	else if (parse_int("macro_repeat_timeout", config->macro_repeat_timeout, s, 0))
		return;
	else if (parse_int("layer_indicator", config->layer_indicator, s, 0, 15))
		return;
	else if (parse_int("overload_tap_timeout", config->overload_tap_timeout, s, 0))
		return;
	else
		warn("[%s] line %zd: %.*s is not a valid global option", file, ln, (int)s.size(), s.data());
}

static void parse_id_section(struct config* config, const char* file, size_t ln, std::string_view s)
{
	if (!s.empty()) {
		uint16_t product, vendor;

		if (s.starts_with('*')) {
			warn("Use k:* to capture keyboards. Wildcard compat mode enabled.");
			config->compat = 1;
			return;
		} else if (s.starts_with("m:*")) {
			config->wildcard |= CAP_MOUSE;
			return;
		} else if (s.starts_with("k:*")) {
			config->wildcard |= CAP_KEYBOARD;
			return;
		} else if (s.starts_with("a:*")) {
			config->wildcard |= CAP_MOUSE_ABS;
			return;
		}

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
			warn("[%s] line %zu: %.*s is not a valid device id", file, ln, (int)s.size(), s.data());
			return;
		}

		memcpy(config->ids.back().id.data(), s.data(), s.size());
	}
}

static void parse_alias_section(struct config* config, const char* file, size_t ln, std::string_view s)
{
	if (!s.empty()) {
		if (auto [desc, next] = lookup_keycode(s); !next.empty()) {
			std::string_view name = get_ini_value(next);
			if (name.size() == 1 && !desc.mods && !desc.wildcard && desc.id < KEYD_ENTRY_COUNT) {
				// Add modifier keys
				if (size_t id = mod_ids.find_first_of(name[0]); id + 1 || name == "-"sv) {
					// Remove this key from any other modifiers
					for (auto& mods : config->modifiers)
						std::erase(mods, desc.id);
					if (id + 1)
						config->modifiers[id].push_back(desc.id);
					return;
				}
			}
			if (name.size()) {
				// TODO: check possibly incorrect names
				auto [alias, _] = lookup_keycode(name);
				if (alias) {
					warn("[%s] line %zu: alias name represents a valid keycode: %.*s", file, ln, (int)name.size(), name.data());
				} else {
					if (alias.wildcard)
						warn("[%s] line %zu: alias contains wildcard, ignored: %.*s", file, ln, (int)name.size(), name.data());
					config->aliases[std::string(name)].emplace_back(desc);
				}
				return;
			}
		} else {
			warn("[%s] line %zu: failed to define alias %.*s (not a valid keycode)", file, ln, (int)s.size(), s.data());
		}
	}
}

void config_null_parser(struct config*, const char*, size_t, std::string_view)
{
}

bool config_parse(struct config *config, const char *path)
{
	// First pass
	size_t chksum0 = 0;
	if (auto section_parser = config_null_parser; !read_ini_file(path, 10, [&](const char* file, size_t ln, std::string_view line) {
		chksum0 ^= std::hash<std::string_view>()(line);
		if (line.starts_with('[') && line.ends_with(']')) {
			if (line == "[ids]")
				section_parser = parse_id_section;
			else if (line == "[global]")
				section_parser = parse_global_section;
			else if (line == "[aliases]")
				section_parser = parse_alias_section;
			else
				section_parser = config_null_parser;
		} else {
			section_parser(config, file, ln, line);
		}
	})) {
		return false;
	}

	// Second pass
	size_t chksum1 = 0;
	if (int layer = -1; !read_ini_file(path, 10, [&](const char* file, size_t ln, std::string_view line) {
		chksum1 ^= std::hash<std::string_view>()(line);
		if (line.starts_with('[') && line.ends_with(']')) {
			if (line == "[ids]" || line == "[global]" || line == "[aliases]") {
				layer = -1;
			} else {
				line.remove_prefix(1);
				line.remove_suffix(1);
				std::string_view name = line;
				name = line.substr(0, name.find_first_of(':'));
				if (name.size() != line.size())
					warn("[%s] line %zu: obsolete layer type specifier: %.*s", file, ln, (int)line.size(), line.data());

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

				layer = name.empty() ? 0 : config_access_layer(config, name);
				if (layer == -1)
					warn("[%.*s] is not a valid layer, ignoring", (int)name.size(), name.data());
			}
		} else if (layer >= 0) {
			if (!set_layer_entry(config, layer, line))
				keyd_log("\tr{ERROR:} [%s] line m{%zd}: %s\n", file, ln, errstr);
		}
	})) {
		return false;
	}

	if (chksum0 != chksum1) {
		warn("Checksums don't match, something did interfere with config files.");
		return false;
	}

	for (auto& layer : config->layers) {
		layer.keymap.sort();
		// TODO: report unreachable layers
		if (layer.keymap.empty() && layer.chords.empty()) {
		}
	}

	config->add_right_wildc = 0;
	config->add_right_mods = 0;
	config->add_left_wildc = 0;
	config->add_left_mods = 0;
	config->pathstr = path;
	return true;
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

int config_add_entry(struct config* config, std::string_view section, std::string_view exp)
{
	int idx = section.empty() ? 0 : config_access_layer(config, section);
	if (idx == -1) {
		err("%.*s is not a valid layer", (int)section.size(), section.data());
		return -1;
	}

	if (!set_layer_entry(config, idx, exp))
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
	, cmd_count(cfg.commands.size())
	, layers(cfg.layers.size())
	, mods(cfg.modifiers)
	, _env(cfg.cmd_env)
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
	cfg.cmd_env = this->_env;
}

config::config()
{
	// Populate special layers
	layers.emplace_back().name = "main";
	layers.emplace_back().name = "alt";
	layers.emplace_back().name = "meta";
	layers.emplace_back().name = "shift";
	layers.emplace_back().name = "control";
	layers.emplace_back().name = "altgr";
	layers.emplace_back().name = "hyper";
	layers.emplace_back().name = "level5";
	layers.emplace_back().name = "mod7";

	modifiers[MOD_ALT].push_back(KEYD_LEFTALT);
	modifiers[MOD_SUPER].push_back(KEYD_LEFTMETA);
	modifiers[MOD_SUPER].push_back(KEYD_RIGHTMETA);
	modifiers[MOD_SHIFT].push_back(KEYD_LEFTSHIFT);
	modifiers[MOD_SHIFT].push_back(KEYD_RIGHTSHIFT);
	modifiers[MOD_CTRL].push_back(KEYD_LEFTCTRL);
	modifiers[MOD_CTRL].push_back(KEYD_RIGHTCTRL);
	modifiers[MOD_ALT_GR].push_back(KEYD_RIGHTALT);

	/* In ms */
	chord_interkey_timeout = 50;
	chord_hold_timeout = 0;
	oneshot_timeout = 0;

	macro_timeout = 600;
	macro_repeat_timeout = 50;
}

config::~config()
{
}

config_backup::~config_backup()
{
}
