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
#include <string_view>
#include <array>
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
	std::vector<descriptor> mapv; // Should be empty by default

	void sort();
	void set(const descriptor& copy, bool sorted);
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
	const_string name;
	descriptor_map keymap;
	std::vector<chord> chords;
	smart_ptr<uint16_t[]> composition;

	size_t size() const
	{
		return composition.size();
	}

	uint16_t* begin() const
	{
		return composition.begin();
	}

	uint16_t* end() const
	{
		return composition.end();
	}
};

struct env_pack {
	std::unique_ptr<char[]> buf;
	std::unique_ptr<const char*[]> env;
	size_t buf_size;
	uid_t uid;
	gid_t gid;

	const char* getenv(std::string_view);

	bool operator==(const env_pack& rhs) const noexcept
	{
		return uid == rhs.uid && gid == rhs.gid && memcmp(buf.get(), rhs.buf.get(), buf_size) == 0;
	}
};

struct ucmd {
	const_string cmd;
	smart_ptr<env_pack> env;

	bool operator==(const ucmd& rhs) const noexcept = default;
};

struct dev_id {
	uint8_t flags;
	std::array<char, 23> id;
};

struct alias {
	uint16_t id;
	uint8_t mods;
	uint8_t wildcard;
};

struct alias_list {
	const_string name;
	smart_ptr<alias[]> list;
};

struct config {
	std::vector<layer> layers;
	std::vector<uint16_t> layer_index;
	std::array<smart_ptr<uint16_t[]>, 8> modifiers;

	bool is_mod(size_t i, uint16_t id) {
		for (auto& mod : modifiers[i]) {
			if (mod == id)
				return true;
		}
		return false;
	}

	/* Auxiliary descriptors used by layer bindings. */
	std::vector<descriptor> descriptors;
	std::vector<macro> macros;
	std::vector<ucmd> commands;

	smart_ptr<alias_list[]> aliases;
	smart_ptr<env_pack> cmd_env;

	std::vector<dev_id> ids;

	int64_t macro_timeout = 600;
	int64_t macro_sequence_timeout = 0;
	int64_t macro_repeat_timeout = 50;
	int64_t oneshot_timeout = 0;

	int64_t overload_tap_timeout = 0;

	int64_t chord_interkey_timeout = 50;
	int64_t chord_hold_timeout = 0;

	bool compat : 1 = false;
	bool finalized : 1 = false;
	uint8_t wildcard = 0;
	uint8_t layer_indicator = 255;
	uint8_t disable_modifier_guard = 0;

	// Section-specific modifiers
	uint8_t add_left_mods = 0;
	uint8_t add_left_wildc = 0;
	uint8_t add_right_mods = 0;
	uint8_t add_right_wildc = 0;
	const_string default_layout;
	const_string pathstr;

	void finalize() noexcept;

	config();
	config(const config&) = delete;
	config& operator=(const config&) = delete;
	~config();
};

struct config_backup {
	struct layer_backup {
		smart_ptr<descriptor[]> keymap;
		smart_ptr<chord[]> chords;
	};

	// These are append-only
	size_t descriptor_count;
	size_t macro_count;
	size_t cmd_count;
	// These ones are nasty
	smart_ptr<layer_backup[]> layers;
	smart_ptr<env_pack> _env;

	explicit config_backup(const struct config& cfg);
	~config_backup();

	void restore(struct keyboard* kbd);
};

bool config_parse(struct config *config, const char *path);
int config_add_entry(struct config *config, std::string_view, std::string_view);

int config_check_match(struct config *config, const char *id, uint8_t flags);

#endif
