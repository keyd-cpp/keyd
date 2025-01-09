#ifndef MACRO_H
#define MACRO_H

#include <stdint.h>
#include <stdlib.h>
#include <memory>
#include <string_view>

enum class macro_e : uint16_t {
	MACRO_KEY_SEQ = 0,
	MACRO_KEY_TAP = 1,
	MACRO_HOLD,
	MACRO_RELEASE,
	MACRO_UNICODE,
	MACRO_TIMEOUT,
	MACRO_COMMAND,

	MACRO_MAX,
};

using enum macro_e;

static_assert(static_cast<uint16_t>(MACRO_MAX) < 64);
static_assert(MACRO_KEY_SEQ < MACRO_KEY_TAP);

struct macro_entry {
	enum macro_e type : 6;
	uint16_t id : 10;
	union {
		struct {
			uint8_t mods;
			uint8_t wildc;
		} mods;
		uint16_t code;
	};
};

static_assert(sizeof(macro_entry) == 4);

/*
 * A series of key sequences, timeouts, shell commands
 */
struct macro {
	uint32_t size;
	macro_entry entry;
	std::unique_ptr<macro_entry[]> entries;

	const macro_entry& operator[](size_t idx) const
	{
		assert(idx < size);
		return size == 1 ? entry : entries[idx];
	}
};

void macro_execute(void (*output)(uint16_t, uint8_t),
		   const macro& macro,
		   size_t timeout, struct config* config);

int macro_parse(std::string_view, macro& macro, struct config* config);
#endif
