/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "keyd.h"
#include "keys.h"
#include "unicode.h"
#include "config.h"
#include "device.h"
#include <memory>
#include <bitset>

#define MAX_ACTIVE_KEYS	32
#define CACHE_SIZE	16 //Effectively nkro

struct keyboard;

struct cache_entry {
	uint16_t code;
	struct descriptor d;
	int16_t dl;
	int16_t layer;
};

struct key_event {
	uint16_t code : 10;
	uint16_t pressed : 1;
	int timestamp;
};

struct output {
	void (*send_key) (uint16_t code, uint8_t state);
	void (*on_layer_change) (const struct keyboard *kbd, struct layer *layer, uint8_t active);
};

enum class chord_state_e : signed char {
	CHORD_RESOLVING,
	CHORD_INACTIVE,
	CHORD_PENDING_DISAMBIGUATION,
	CHORD_PENDING_HOLD_TIMEOUT,
};

using enum chord_state_e;

enum class pending_behaviour_e : signed char {
	PK_INTERRUPT_ACTION1,
	PK_INTERRUPT_ACTION2,
	PK_UNINTERRUPTIBLE,
	PK_UNINTERRUPTIBLE_TAP_ACTION2,
};

using enum pending_behaviour_e;

struct active_chord {
	uint8_t active;
	struct chord chord;
	int layer;
};

/* May correspond to more than one physical input device. */
struct keyboard {
	std::vector<config_backup> original_config;
	struct config config;
	struct output output;

	/*
	 * Cache descriptors to preserve code->descriptor
	 * mappings in the event of mid-stroke layer changes.
	 */
	struct cache_entry cache[CACHE_SIZE];

	int16_t layout = 0;

	uint16_t last_pressed_output_code;
	uint16_t last_pressed_code;

	uint16_t oneshot_latch;

	uint16_t inhibit_modifier_guard;

	int active_macro = -1;
	int active_macro_layer;
	int overload_last_layer_code;

	int64_t macro_timeout;
	int64_t oneshot_timeout;

	int64_t macro_repeat_interval;

	int64_t overload_start_time;

	int64_t last_simple_key_time;

	int64_t timeouts[64];
	size_t nr_timeouts;

	struct active_chord active_chords[KEYD_CHORD_MAX-KEYD_CHORD_1+1];

	struct {
		struct key_event queue[32];
		size_t queue_sz;

		const struct chord *match;
		int match_layer;

		uint16_t start_code;
		int64_t last_code_time;

		enum chord_state_e state;
	} chord;

	struct {
		uint16_t code;
		int16_t dl;
		int64_t expire;
		int64_t tap_expiry;

		enum pending_behaviour_e behaviour;

		struct key_event queue[32];
		size_t queue_sz;

		struct descriptor action1;
		struct descriptor action2;
	} pending_key{};

	struct layer_state_t {
		uint64_t composite : 1; // Set to 1 if layer is composite and not "empty"
		uint64_t active_s : 8; // Activation count (signed)
		uint64_t toggled : 1;
		uint64_t oneshot_depth : 7;
		uint64_t activation_time : (64 - 17); // Activation order, not timestamp

		bool active() const
		{
			return static_cast<int8_t>(active_s & 0xff) > 0;
		}
	};
	static_assert(sizeof(layer_state_t) == 8);
	std::vector<layer_state_t> layer_state;
	std::vector<uint16_t> active_layers; // Currently not updated, just a buffer

	void update_layer_state()
	{
		layer_state.resize(config.layers.size());
		active_layers.resize(config.layers.size());
		for (size_t i = 0; i < layer_state.size(); i++) {
			auto& layer = config.layers[i];
			// Cache whether the layer is truly composite (not dummy)
			layer_state[i].composite = !layer.name[0] && (!layer.keymap.empty() || !layer.chords.empty());
		}
	}

	std::bitset<KEYD_ENTRY_COUNT> capstate; // Input state
	std::bitset<KEYD_ENTRY_COUNT> keystate; // Vkbd state

	struct {
		int x;
		int y;

		int sensitivity; /* Mouse units per scroll unit (higher == slower scrolling). */
		int active;
	} scroll;
};

std::unique_ptr<keyboard> new_keyboard(std::unique_ptr<keyboard>);

int64_t kbd_process_events(struct keyboard *kbd, const struct key_event *events, size_t n, bool real = false);
bool kbd_eval(struct keyboard *kbd, std::string_view);
void kbd_reset(struct keyboard *kbd);

#endif
