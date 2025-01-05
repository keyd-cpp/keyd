/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#include <stdint.h>
#include <string.h>
#include "keys.h"
#include <array>

#ifdef __FreeBSD__
	#include <dev/evdev/input.h>
#else
	#include <linux/input.h>
#endif

extern constexpr std::array<keycode_table_ent, KEYD_ENTRY_COUNT> keycode_table = []() {
	std::array<keycode_table_ent, KEYD_ENTRY_COUNT> r{};
	r[0] = { "autokey", "auto", nullptr };
	r[KEY_ESC] = { "esc", "escape", NULL },
	r[KEY_1] = { "1", NULL, "!" },
	r[KEY_2] = { "2", NULL, "@" },
	r[KEY_3] = { "3", NULL, "#" },
	r[KEY_4] = { "4", NULL, "$" },
	r[KEY_5] = { "5", NULL, "%" },
	r[KEY_6] = { "6", NULL, "^" },
	r[KEY_7] = { "7", NULL, "&" },
	r[KEY_8] = { "8", NULL, "*" },
	r[KEY_9] = { "9", NULL, "(" },
	r[KEY_0] = { "0", NULL, ")" },
	r[KEY_MINUS] = { "-", "minus", "_" },
	r[KEY_EQUAL] = { "=", "equal", "+" },
	r[KEY_BACKSPACE] = { "backspace", "\b", NULL },
	r[KEY_TAB] = { "tab", "\t", NULL },
	r[KEY_Q] = { "q", NULL, "Q" },
	r[KEY_W] = { "w", NULL, "W" },
	r[KEY_E] = { "e", NULL, "E" },
	r[KEY_R] = { "r", NULL, "R" },
	r[KEY_T] = { "t", NULL, "T" },
	r[KEY_Y] = { "y", NULL, "Y" },
	r[KEY_U] = { "u", NULL, "U" },
	r[KEY_I] = { "i", NULL, "I" },
	r[KEY_O] = { "o", NULL, "O" },
	r[KEY_P] = { "p", NULL, "P" },
	r[KEY_LEFTBRACE] = { "[", "leftbrace", "{" },
	r[KEY_RIGHTBRACE] = { "]", "rightbrace", "}" },
	r[KEY_ENTER] = { "enter", "\n", NULL },
	r[KEY_LEFTCTRL] = { "leftcontrol", "leftctrl", NULL },
	r[KEYD_IS_LEVEL3_SHIFT] = { "iso-level3-shift", "level3", NULL },
	r[KEY_A] = { "a", NULL, "A" },
	r[KEY_S] = { "s", NULL, "S" },
	r[KEY_D] = { "d", NULL, "D" },
	r[KEY_F] = { "f", NULL, "F" },
	r[KEY_G] = { "g", NULL, "G" },
	r[KEY_H] = { "h", NULL, "H" },
	r[KEY_J] = { "j", NULL, "J" },
	r[KEY_K] = { "k", NULL, "K" },
	r[KEY_L] = { "l", NULL, "L" },
	r[KEY_SEMICOLON] = { ";", "semicolon", ":" },
	r[KEY_APOSTROPHE] = { "'", "apostrophe", "\"" },
	r[KEY_GRAVE] = { "`", "grave", "~" },
	r[KEY_LEFTSHIFT] = { "leftshift", nullptr, NULL },
	r[KEY_BACKSLASH] = { "\\", "backslash", "|" },
	r[KEY_Z] = { "z", NULL, "Z" },
	r[KEY_X] = { "x", NULL, "X" },
	r[KEY_C] = { "c", NULL, "C" },
	r[KEY_V] = { "v", NULL, "V" },
	r[KEY_B] = { "b", NULL, "B" },
	r[KEY_N] = { "n", NULL, "N" },
	r[KEY_M] = { "m", NULL, "M" },
	r[KEY_COMMA] = { ",", "comma", "<" },
	r[KEY_DOT] = { ".", "dot", ">" },
	r[KEY_SLASH] = { "/", "slash", "?" },
	r[KEY_RIGHTSHIFT] = { "rightshift", NULL, NULL },
	r[KEY_KPASTERISK] = { "kpasterisk", NULL, NULL },
	r[KEY_LEFTALT] = { "leftalt", nullptr, NULL },
	r[KEY_SPACE] = { "space", " ", NULL },
	r[KEY_CAPSLOCK] = { "capslock", NULL, NULL },
	r[KEY_F1] = { "f1", NULL, NULL },
	r[KEY_F2] = { "f2", NULL, NULL },
	r[KEY_F3] = { "f3", NULL, NULL },
	r[KEY_F4] = { "f4", NULL, NULL },
	r[KEY_F5] = { "f5", NULL, NULL },
	r[KEY_F6] = { "f6", NULL, NULL },
	r[KEY_F7] = { "f7", NULL, NULL },
	r[KEY_F8] = { "f8", NULL, NULL },
	r[KEY_F9] = { "f9", NULL, NULL },
	r[KEY_F10] = { "f10", NULL, NULL },
	r[KEY_NUMLOCK] = { "numlock", NULL, NULL },
	r[KEY_SCROLLLOCK] = { "scrolllock", NULL, NULL },
	r[KEY_KP7] = { "kp7", NULL, NULL },
	r[KEY_KP8] = { "kp8", NULL, NULL },
	r[KEY_KP9] = { "kp9", NULL, NULL },
	r[KEY_KPMINUS] = { "kpminus", NULL, NULL },
	r[KEY_KP4] = { "kp4", NULL, NULL },
	r[KEY_KP5] = { "kp5", NULL, NULL },
	r[KEY_KP6] = { "kp6", NULL, NULL },
	r[KEY_KPPLUS] = { "kpplus", NULL, NULL },
	r[KEY_KP1] = { "kp1", NULL, NULL },
	r[KEY_KP2] = { "kp2", NULL, NULL },
	r[KEY_KP3] = { "kp3", NULL, NULL },
	r[KEY_KP0] = { "kp0", NULL, NULL },
	r[KEY_KPDOT] = { "kpdot", NULL, NULL },
	r[KEY_ZENKAKUHANKAKU] = { "zenkakuhankaku", NULL, NULL },
	r[KEY_102ND] = { "102nd", NULL, NULL },
	r[KEY_F11] = { "f11", NULL, NULL },
	r[KEY_F12] = { "f12", NULL, NULL },
	r[KEY_RO] = { "ro", NULL, NULL },
	r[KEY_KATAKANA] = { "katakana", NULL, NULL },
	r[KEY_HIRAGANA] = { "hiragana", NULL, NULL },
	r[KEY_HENKAN] = { "henkan", NULL, NULL },
	r[KEY_KATAKANAHIRAGANA] = { "katakanahiragana", NULL, NULL },
	r[KEY_MUHENKAN] = { "muhenkan", NULL, NULL },
	r[KEY_KPJPCOMMA] = { "kpjpcomma", NULL, NULL },
	r[KEY_KPENTER] = { "kpenter", NULL, NULL },
	r[KEY_RIGHTCTRL] = { "rightcontrol", "rightctrl", NULL },
	r[KEY_KPSLASH] = { "kpslash", NULL, NULL },
	r[KEY_SYSRQ] = { "sysrq", NULL, NULL },
	r[KEY_RIGHTALT] = { "rightalt", NULL, NULL },
	r[KEY_LINEFEED] = { "linefeed", NULL, NULL },
	r[KEY_HOME] = { "home", NULL, NULL },
	r[KEY_UP] = { "up", NULL, NULL },
	r[KEY_PAGEUP] = { "pageup", NULL, NULL },
	r[KEY_LEFT] = { "left", NULL, NULL },
	r[KEY_RIGHT] = { "right", NULL, NULL },
	r[KEY_END] = { "end", NULL, NULL },
	r[KEY_DOWN] = { "down", NULL, NULL },
	r[KEY_PAGEDOWN] = { "pagedown", NULL, NULL },
	r[KEY_INSERT] = { "insert", NULL, NULL },
	r[KEY_DELETE] = { "delete", NULL, NULL },
	r[KEY_MACRO] = { "macro", NULL, NULL },
	r[KEY_MUTE] = { "mute", NULL, NULL },
	r[KEY_VOLUMEDOWN] = { "volumedown", NULL, NULL },
	r[KEY_VOLUMEUP] = { "volumeup", NULL, NULL },
	r[KEY_POWER] = { "power", NULL, NULL },
	r[KEY_KPEQUAL] = { "kpequal", NULL, NULL },
	r[KEY_KPPLUSMINUS] = { "kpplusminus", NULL, NULL },
	r[KEY_PAUSE] = { "pause", NULL, NULL },
	r[KEY_SCALE] = { "scale", NULL, NULL },
	r[KEY_KPCOMMA] = { "kpcomma", NULL, NULL },
	r[KEY_HANGEUL] = { "hangeul", NULL, NULL },
	r[KEY_HANJA] = { "hanja", NULL, NULL },
	r[KEY_YEN] = { "yen", NULL, NULL },
	r[KEY_LEFTMETA] = { "leftmeta", "leftsuper", NULL },
	r[KEY_RIGHTMETA] = { "rightmeta", "rightsuper", NULL },
	r[KEY_COMPOSE] = { "compose", NULL, NULL },
	r[KEY_STOP] = { "stop", NULL, NULL },
	r[KEY_AGAIN] = { "again", NULL, NULL },
	r[KEY_PROPS] = { "props", NULL, NULL },
	r[KEY_UNDO] = { "undo", NULL, NULL },
	r[KEY_FRONT] = { "front", NULL, NULL },
	r[KEY_COPY] = { "copy", NULL, NULL },
	r[KEY_OPEN] = { "open", NULL, NULL },
	r[KEY_PASTE] = { "paste", NULL, NULL },
	r[KEY_FIND] = { "find", NULL, NULL },
	r[KEY_CUT] = { "cut", NULL, NULL },
	r[KEY_HELP] = { "help", NULL, NULL },
	r[KEY_MENU] = { "menu", NULL, NULL },
	r[KEY_CALC] = { "calc", NULL, NULL },
	r[KEY_SETUP] = { "setup", NULL, NULL },
	r[KEY_SLEEP] = { "sleep", NULL, NULL },
	r[KEY_WAKEUP] = { "wakeup", NULL, NULL },
	r[KEY_FILE] = { "file", NULL, NULL },
	r[KEY_SENDFILE] = { "sendfile", NULL, NULL },
	r[KEY_DELETEFILE] = { "deletefile", NULL, NULL },
	r[KEY_XFER] = { "xfer", NULL, NULL },
	r[KEY_PROG1] = { "prog1", NULL, NULL },
	r[KEY_PROG2] = { "prog2", NULL, NULL },
	r[KEY_WWW] = { "www", NULL, NULL },
	r[KEY_MSDOS] = { "msdos", NULL, NULL },
	r[KEY_COFFEE] = { "coffee", NULL, NULL },
	r[KEY_ROTATE_DISPLAY] = { "display", NULL, NULL },
	r[KEY_CYCLEWINDOWS] = { "cyclewindows", NULL, NULL },
	r[KEY_MAIL] = { "mail", NULL, NULL },
	r[KEY_BOOKMARKS] = { "bookmarks", NULL, NULL },
	r[KEY_COMPUTER] = { "computer", NULL, NULL },
	r[KEY_BACK] = { "back", NULL, NULL },
	r[KEY_FORWARD] = { "forward", NULL, NULL },
	r[KEY_CLOSECD] = { "closecd", NULL, NULL },
	r[KEY_EJECTCD] = { "ejectcd", NULL, NULL },
	r[KEY_EJECTCLOSECD] = { "ejectclosecd", NULL, NULL },
	r[KEY_NEXTSONG] = { "nextsong", NULL, NULL },
	r[KEY_PLAYPAUSE] = { "playpause", NULL, NULL },
	r[KEY_PREVIOUSSONG] = { "previoussong", NULL, NULL },
	r[KEY_STOPCD] = { "stopcd", NULL, NULL },
	r[KEY_RECORD] = { "record", NULL, NULL },
	r[KEY_REWIND] = { "rewind", NULL, NULL },
	r[KEY_PHONE] = { "phone", NULL, NULL },
	r[KEY_ISO] = { "iso", NULL, NULL },
	r[KEY_CONFIG] = { "config", NULL, NULL },
	r[KEY_HOMEPAGE] = { "homepage", NULL, NULL },
	r[KEY_REFRESH] = { "refresh", NULL, NULL },
	r[KEY_EXIT] = { "exit", NULL, NULL },
	r[KEY_MOVE] = { "move", NULL, NULL },
	r[KEY_EDIT] = { "edit", NULL, NULL },
	r[KEY_SCROLLUP]	= { "scrollup", nullptr, nullptr },
	r[KEY_SCROLLDOWN] = { "scrolldown", nullptr, nullptr },
	r[KEY_KPLEFTPAREN] = { "kpleftparen", NULL, NULL },
	r[KEY_KPRIGHTPAREN] = { "kprightparen", NULL, NULL },
	r[KEY_NEW] = { "new", NULL, NULL },
	r[KEY_REDO] = { "redo", NULL, NULL },
	r[KEY_F13] = { "f13", NULL, NULL },
	r[KEY_F14] = { "f14", NULL, NULL },
	r[KEY_F15] = { "f15", NULL, NULL },
	r[KEY_F16] = { "f16", NULL, NULL },
	r[KEY_F17] = { "f17", NULL, NULL },
	r[KEY_F18] = { "f18", NULL, NULL },
	r[KEY_F19] = { "f19", NULL, NULL },
	r[KEY_F20] = { "f20", NULL, NULL },
	r[KEY_F21] = { "f21", NULL, NULL },
	r[KEY_F22] = { "f22", NULL, NULL },
	r[KEY_F23] = { "f23", NULL, NULL },
	r[KEY_F24] = { "f24", NULL, NULL },
	r[KEY_PLAYCD] = { "playcd", NULL, NULL },
	r[KEY_PAUSECD] = { "pausecd", NULL, NULL },
	r[KEY_PROG3] = { "prog3", NULL, NULL },
	r[KEY_PROG4] = { "prog4", NULL, NULL },
	r[KEY_DASHBOARD] = { "dashboard", NULL, NULL },
	r[KEY_SUSPEND] = { "suspend", NULL, NULL },
	r[KEY_CLOSE] = { "close", NULL, NULL },
	r[KEY_PLAY] = { "play", NULL, NULL },
	r[KEY_FASTFORWARD] = { "fastforward", NULL, NULL },
	r[KEY_BASSBOOST] = { "bassboost", NULL, NULL },
	r[KEY_PRINT] = { "print", NULL, NULL },
	r[KEY_HP] = { "hp", NULL, NULL },
	r[KEY_CAMERA] = { "camera", NULL, NULL },
	r[KEY_SOUND] = { "sound", NULL, NULL },
	r[KEY_QUESTION] = { "question", NULL, NULL },
	r[KEY_EMAIL] = { "email", NULL, NULL },
	r[KEY_CHAT] = { "chat", NULL, NULL },
	r[KEY_SEARCH] = { "search", NULL, NULL },
	r[KEY_CONNECT] = { "connect", NULL, NULL },
	r[KEY_FINANCE] = { "finance", NULL, NULL },
	r[KEY_SPORT] = { "sport", NULL, NULL },
	r[KEY_SHOP] = { "shop", NULL, NULL },
	r[KEY_VOICECOMMAND] = { "voicecommand", NULL, NULL },
	r[KEY_CANCEL] = { "cancel", NULL, NULL },
	r[KEY_BRIGHTNESSDOWN] = { "brightnessdown", NULL, NULL },
	r[KEY_BRIGHTNESSUP] = { "brightnessup", NULL, NULL },
	r[KEY_MEDIA] = { "media", NULL, NULL },
	r[KEY_SWITCHVIDEOMODE] = { "switchvideomode", NULL, NULL },
	r[KEY_KBDILLUMTOGGLE] = { "kbdillumtoggle", NULL, NULL },
	r[KEY_KBDILLUMDOWN] = { "kbdillumdown", NULL, NULL },
	r[KEY_KBDILLUMUP] = { "kbdillumup", NULL, NULL },
	r[KEY_SEND] = { "send", NULL, NULL },
	r[KEY_REPLY] = { "reply", NULL, NULL },
	r[KEY_FORWARDMAIL] = { "forwardmail", NULL, NULL },
	r[KEY_SAVE] = { "save", NULL, NULL },
	r[KEY_DOCUMENTS] = { "documents", NULL, NULL },
	r[KEY_BATTERY] = { "battery", NULL, NULL },
	r[KEY_BLUETOOTH] = { "bluetooth", NULL, NULL },
	r[KEY_WLAN] = { "wlan", NULL, NULL },
	r[KEY_UWB] = { "uwb", NULL, NULL },
	r[KEY_UNKNOWN] = { "unknown", NULL, NULL },
	r[KEY_VIDEO_NEXT] = { "next", NULL, NULL },
	r[KEY_VIDEO_PREV] = { "prev", NULL, NULL },
	r[KEY_BRIGHTNESS_CYCLE] = { "cycle", NULL, NULL },
	r[KEY_BRIGHTNESS_AUTO] = { "auto", NULL, NULL },
	r[KEY_DISPLAY_OFF] = { "off", NULL, NULL },
	r[KEY_WWAN] = { "wwan", NULL, NULL },
	r[KEY_RFKILL] = { "rfkill", NULL, NULL },
	r[KEY_MICMUTE] = { "micmute", NULL, NULL },
	r[KEYD_LEFT_MOUSE] = { "leftmouse", NULL, NULL },
	r[KEYD_RIGHT_MOUSE] = { "rightmouse", NULL, NULL },
	r[KEYD_MIDDLE_MOUSE] = { "middlemouse", NULL, NULL },
	r[KEYD_MOUSE_1] = { "mouse1", NULL, NULL },
	r[KEYD_MOUSE_2] = { "mouse2", NULL, NULL },
	r[KEYD_MOUSE_BACK] = { "mouseback", NULL, NULL },
	r[KEYD_MOUSE_FORWARD] = { "mouseforward", NULL, NULL },
	r[KEYD_NOOP] = { "noop", NULL, NULL };
	r[KEY_FN] = { "fn", NULL, NULL },
	r[KEY_ZOOM] = { "zoom", NULL, NULL },
	r[KEYD_FAKEMOD_ALT] = { "fakealt", nullptr, nullptr },
	r[KEYD_FAKEMOD_SUPER] = { "fakemeta", "fakesuper", nullptr },
	r[KEYD_FAKEMOD_SHIFT] = { "fakeshift", nullptr, nullptr },
	r[KEYD_FAKEMOD_CTRL] = { "fakecontrol", "fakectrl", nullptr },
	r[KEYD_FAKEMOD_ALTGR] = { "fakealtgr", nullptr, nullptr },
	r[KEYD_FAKEMOD_HYPER] = { "fakehyper", nullptr, nullptr },
	r[KEYD_FAKEMOD_LEVEL5] = { "fakelevel5", nullptr, nullptr },
	r[KEYD_FAKEMOD_NUMLOCK] = { "fakemod7", "fakenlock", nullptr },
	void();

	for (size_t i = 1; i < KEYD_ENTRY_COUNT; i++) {
		r[i].key_num[4] = '0' + (i / 100) % 10;
		r[i].key_num[5] = '0' + (i / 10) % 10;
		r[i].key_num[6] = '0' + (i % 10);
	}
	return r;
}();

std::array<char, 16> modstring(uint8_t mods)
{
	std::array<char, 16> s{};
	int i = 0;

	for (size_t j = 0; j < mod_ids.size(); j++) {
		if (mods & (1 << j)) {
			s[i++] = mod_ids[j];
			s[i++] = '-';
		}
	}

	return s;
}

int parse_key_sequence(std::string_view s, uint16_t* codep, uint8_t *modsp, uint8_t* wcsp)
{
	auto c = s;
	if (s.empty())
		return -1;

	uint8_t wildcard = 0;
	uint8_t mods = 0;
	while (c.size() >= 2) {
		if (c.starts_with("**")) {
			wildcard = -1;
		} else if (size_t id = mod_ids.find_first_of(c[0]); id + 1 && c[1] == '*') {
			wildcard |= 1 << id;
		} else if (id + 1 && c[1] == '-') {
			mods |= 1 << id;
		} else
			break;
		c.remove_prefix(2);
	}

	// Allow partial success
	if (modsp)
		*modsp = mods;

	if (codep)
		*codep = 0;

	if (wcsp)
		*wcsp = wildcard;

	for (size_t i = 0; i < KEYD_KEY_COUNT; i++) {
		const struct keycode_table_ent *ent = &keycode_table[i];

		if (true) {
			if (ent->shifted_name && ent->shifted_name == c) {

				mods |= MOD_SHIFT;

				if (modsp)
					*modsp = mods;

				if (codep)
					*codep = i;

				return 0;
			} else if (ent->name() == c || ent->key_num == c || (ent->alt_name && ent->alt_name == c)) {

				if (modsp)
					*modsp = mods;

				if (codep)
					*codep = i;

				return 0;
			}
		}
	}

	// Return number of remaining bytes for partial success
	return c.size();
}
