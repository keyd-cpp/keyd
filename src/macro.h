#ifndef MACRO_H
#define MACRO_H

#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include <string_view>

enum class macro_e : uint16_t {
	MACRO_KEYSEQUENCE,
	MACRO_HOLD,
	MACRO_RELEASE,
	MACRO_UNICODE,
	MACRO_TIMEOUT,
	MACRO_COMMAND,

	MACRO_MAX,
};

using enum macro_e;

static_assert(static_cast<uint16_t>(MACRO_MAX) < 64);

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
 * A series of key sequences optionally punctuated by
 * timeouts
 */
using macro = std::vector<macro_entry>;

void macro_execute(void (*output)(uint16_t, uint8_t),
		   const macro& macro,
		   size_t timeout, struct config* config);

int macro_parse(std::string_view, macro& macro, struct config* config);
#endif
