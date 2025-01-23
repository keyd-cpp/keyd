/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#ifndef _KEYS_H_
#define _KEYS_H_
#define _KEYS_H_

#include <stdint.h>
#include <stdlib.h>
#include <string_view>
#include <array>

#ifdef __FreeBSD__
	#include <dev/evdev/input.h>
#else
	#include <linux/input.h>
#endif

#define MOD_NLOCK 7
#define MOD_LEVEL5 6
#define MOD_HYPER 5
#define MOD_ALT_GR 4
#define MOD_CTRL 3
#define MOD_SHIFT 2
#define MOD_SUPER 1
#define MOD_ALT 0

// Mod codes
#define MOD_IDS "AMSCGHLN"
constexpr std::string_view mod_ids = MOD_IDS;

#define MAX_MOD 8
#define MOD_MAX 8

struct keycode_table_ent {
	const char* b_name = nullptr;
	const char* alt_name = nullptr;
	const char* shifted_name = nullptr;
	char key_num[8]{'k', 'e', 'y', '_', '0', '0', '0'};

	constexpr std::string_view name() const noexcept
	{
		return b_name ? b_name : std::string_view(key_num, 7);
	}
};

struct modifier {
	uint8_t mask;
	uint16_t key;
};

/* Deviations (TODO: use special value range)*/

#define  KEYD_CHORD_1                  197
#define  KEYD_CHORD_2                  198
#define  KEYD_CHORD_MAX                        199

// /* Special values. */

#define KEYD_WHEELUP			0x300
#define KEYD_WHEELDOWN			0x301
#define KEYD_WHEELLEFT			0x302
#define KEYD_WHEELRIGHT			0x303
#define KEYD_WHEELEVENT(x)		((x & -4) == KEYD_WHEELUP)

#define KEYD_FAKEMOD			900
#define KEYD_FAKEMOD_ALT		900
#define KEYD_FAKEMOD_SUPER		901
#define KEYD_FAKEMOD_SHIFT		902
#define KEYD_FAKEMOD_CTRL		903
#define KEYD_FAKEMOD_ALTGR		904
#define KEYD_FAKEMOD_HYPER		905
#define KEYD_FAKEMOD_LEVEL5		906
#define KEYD_FAKEMOD_NLOCK		907

#define KEYD_NOOP 				(KEYD_ENTRY_COUNT-1)

#define KEY_NAME(code) (size_t(code) < KEYD_ENTRY_COUNT ? keycode_table[code].name().data() : "UNKNOWN")

int parse_key_sequence(std::string_view, uint16_t* code, uint8_t *mods, uint8_t* wildcards = nullptr);

#define KEYD_ENTRY_COUNT			1000

extern const std::array<keycode_table_ent, KEYD_ENTRY_COUNT> keycode_table;

std::array<char, 16> modstring(uint8_t mods);

#endif
