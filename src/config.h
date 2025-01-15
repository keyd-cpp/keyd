/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <limits.h>
#include "macro.h"
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <array>
#include <map>
#include "utils.hpp"

#define MAX_DESCRIPTOR_ARGS	3

#define ID_EXCLUDED	1
#define ID_MOUSE	2
#define ID_KEYBOARD	4
#define ID_ABS_PTR	8

enum class op : uint16_t {
	OP_NULL = 0,
	OP_KEYSEQUENCE = 1,

	OP_ONESHOT,
	OP_ONESHOTM,
	OP_LAYERM,
	OP_SWAP,
	OP_SWAPM,
	OP_LAYER,
	OP_LAYOUT,
	OP_CLEAR,
	OP_CLEARM,
	OP_OVERLOAD,
	OP_OVERLOADM,
	OP_OVERLOAD_TIMEOUT,
	OP_OVERLOAD_TIMEOUT_TAP,
	OP_OVERLOAD_IDLE_TIMEOUT,
	OP_TOGGLE,
	OP_TOGGLEM,

	OP_MACRO,
	OP_MACRO2,
	OP_TIMEOUT,

/* Experimental */
	OP_SCROLL_TOGGLE,
	OP_SCROLL,

	OP_MAX,
};

using enum op;

static_assert(static_cast<uint16_t>(OP_MAX) <= 64);

union descriptor_arg {
	uint16_t code;
	uint8_t mods;
	uint8_t wildc;
	int16_t idx;
	uint16_t sz;
	uint16_t timeout;
	int16_t sensitivity;
};

/* Describes the intended purpose of a key (corresponds to an 'action' in user parlance). */

struct descriptor {
	enum op op : 6;
	uint16_t id : 10; // Key associated with descriptor
	uint8_t mods; // Mods associated with descriptor
	uint8_t wildcard; // Mod mask that allows mod(s) to be enabled
	std::array<union descriptor_arg, MAX_DESCRIPTOR_ARGS> args;

	bool operator <(const descriptor&) const;
	bool operator ==(const descriptor&) const;

	// Full deep comparison
	bool equals(const struct config* cfg, const descriptor& rhs) const;

	explicit operator bool() const noexcept
	{
		return op != OP_NULL;
	}
};

static_assert(sizeof(descriptor) == 10);

// Experimental flat map with deferred sorting for layer keymap descriptors
struct descriptor_map {
	// uint8_t size = 0;
	// static constexpr uint8_t unsorted = -1;
	// static constexpr uint8_t dynamic = -2;
	// std::array<descriptor, 3> maps{};

	std::vector<descriptor> mapv; // Should be empty by default

	void sort();
	void set(const descriptor& copy, uint32_t hint = 48);
	const descriptor& operator[](const descriptor&) const;

	bool empty() const { return mapv.empty(); }
};

static_assert(sizeof(descriptor_map) == sizeof(std::vector<char>));

struct chord {
	std::array<uint16_t, 8> keys;
	struct descriptor d;
};

static_assert(sizeof(chord) == 26);

/*
 * A layer is a map from keys to descriptors.
 */

struct layer {
	std::string name;
	std::vector<chord> chords;
	descriptor_map keymap;

	// Iterator over composite layer components
	struct iterator
	{
		using difference_type = ptrdiff_t;
		using value_type = uint16_t;
		using pointer = void;
		using reference = uint16_t;
		using iterator_category = std::forward_iterator_tag;

		const char* raw_ptr;

		uint16_t operator*() const
		{
			// Unaligned read from name
			uint16_t value;
			memcpy(&value, this->raw_ptr, sizeof(value));
			return value;
		}

		iterator& operator++()
		{
			this->raw_ptr += 2;
			return *this;
		}

		auto operator<=>(const iterator&) const = default;
	};

	size_t size() const
	{
		return name[0] ? 1 : name.size() / 2;
	}

	iterator begin() const
	{
		return {name.c_str() + (name[0] ? name.size() : 1)};
	}

	iterator end() const
	{
		return {name.c_str() + name.size()};
	}
};

struct env_pack {
	std::unique_ptr<char[]> buf;
	std::unique_ptr<const char*[]> env;
	size_t buf_size;
	uid_t uid;
	gid_t gid;

	const char* getenv(std::string_view);

	bool operator==(const env_pack& rhs) const
	{
		return uid == rhs.uid && gid == rhs.gid && memcmp(buf.get(), rhs.buf.get(), buf_size) == 0;
	}
};

struct ucmd {
	std::string cmd;
	smart_ptr<env_pack> env;

	bool operator==(const ucmd&) const = default;
};

struct dev_id {
	uint8_t flags;
	std::array<char, 23> id;
};

struct config {
	std::vector<layer> layers;
	std::vector<uint16_t> layer_index;
	std::array<std::u16string, 8> modifiers;

	/* Auxiliary descriptors used by layer bindings. */
	std::vector<descriptor> descriptors;
	std::vector<macro> macros;
	std::vector<ucmd> commands;
	std::map<std::string, std::vector<descriptor>, std::less<>> aliases;

	smart_ptr<env_pack> cmd_env;

	std::vector<dev_id> ids;

	int64_t macro_timeout;
	int64_t macro_sequence_timeout;
	int64_t macro_repeat_timeout;
	int64_t oneshot_timeout;

	int64_t overload_tap_timeout;

	int64_t chord_interkey_timeout;
	int64_t chord_hold_timeout;

	uint8_t compat = 0;
	uint8_t wildcard = 0;
	uint8_t layer_indicator = 255;
	uint8_t disable_modifier_guard = 0;

	// Section-specific modifiers
	uint8_t add_left_mods = 0;
	uint8_t add_left_wildc = 0;
	uint8_t add_right_mods = 0;
	uint8_t add_right_wildc = 0;
	std::string default_layout;
	std::string pathstr;

	config();
	config(const config&) = delete;
	config& operator=(const config&) = delete;
	~config();
};

struct config_backup {
	struct layer_backup {
		decltype(layer::keymap) keymap;
		decltype(layer::chords) chords;
	};

	// These are append-only
	size_t descriptor_count;
	size_t macro_count;
	size_t cmd_count;
	// These ones are nasty
	std::vector<layer_backup> layers;
	decltype(config::modifiers) mods;
	smart_ptr<env_pack> _env;

	explicit config_backup(const struct config& cfg);
	~config_backup();

	void restore(struct keyboard* kbd);
};

bool config_parse(struct config *config, const char *path);
int config_add_entry(struct config *config, std::string_view, std::string_view);

int config_check_match(struct config *config, const char *id, uint8_t flags);

#endif
