/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 * © 2021 Giorgi Chavchanidze
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "../keyd.h"
#include "../keys.h"
#include "usb-gadget.h"

#ifdef __FreeBSD__
	#include <dev/evdev/uinput.h>
	#include <dev/evdev/input-event-codes.h>
#else
	#include <linux/uinput.h>
	#include <linux/input-event-codes.h>
#endif

constexpr std::array<uint8_t, KEYD_ENTRY_COUNT> hid_table = []() {
	std::array<uint8_t, KEYD_ENTRY_COUNT> r{};
	r[KEY_ESC] = 0x29,
	r[KEY_1] = 0x1e,
	r[KEY_2] = 0x1f,
	r[KEY_3] = 0x20,
	r[KEY_4] = 0x21,
	r[KEY_5] = 0x22,
	r[KEY_6] = 0x23,
	r[KEY_7] = 0x24,
	r[KEY_8] = 0x25,
	r[KEY_9] = 0x26,
	r[KEY_0] = 0x27,
	r[KEY_MINUS] = 0x2d,
	r[KEY_EQUAL] = 0x2e,
	r[KEY_BACKSPACE] = 0x2a,
	r[KEY_TAB] = 0x2b,
	r[KEY_Q] = 0x14,
	r[KEY_W] = 0x1a,
	r[KEY_E] = 0x08,
	r[KEY_R] = 0x15,
	r[KEY_T] = 0x17,
	r[KEY_Y] = 0x1c,
	r[KEY_U] = 0x18,
	r[KEY_I] = 0x0c,
	r[KEY_O] = 0x12,
	r[KEY_P] = 0x13,
	r[KEY_LEFTBRACE] = 0x2f,
	r[KEY_RIGHTBRACE] = 0x30,
	r[KEY_ENTER] = 0x28,
	r[KEY_LEFTCTRL] = 0xe0,
	r[KEY_A] = 0x04,
	r[KEY_S] = 0x16,
	r[KEY_D] = 0x07,
	r[KEY_F] = 0x09,
	r[KEY_G] = 0x0a,
	r[KEY_H] = 0x0b,
	r[KEY_J] = 0x0d,
	r[KEY_K] = 0x0e,
	r[KEY_L] = 0x0f,
	r[KEY_SEMICOLON] = 0x33,
	r[KEY_APOSTROPHE] = 0x34,
	r[KEY_GRAVE] = 0x35,
	r[KEY_LEFTSHIFT] = 0xe1,
	r[KEY_BACKSLASH] = 0x31,
	r[KEY_Z] = 0x1d,
	r[KEY_X] = 0x1b,
	r[KEY_C] = 0x06,
	r[KEY_V] = 0x19,
	r[KEY_B] = 0x05,
	r[KEY_N] = 0x11,
	r[KEY_M] = 0x10,
	r[KEY_COMMA] = 0x36,
	r[KEY_DOT] = 0x37,
	r[KEY_SLASH] = 0x38,
	r[KEY_RIGHTSHIFT] = 0xe5,
	r[KEY_KPASTERISK] = 0x55,
	r[KEY_LEFTALT] = 0xe2,
	r[KEY_SPACE] = 0x2c,
	r[KEY_CAPSLOCK] = 0x39,
	r[KEY_F1] = 0x3a,
	r[KEY_F2] = 0x3b,
	r[KEY_F3] = 0x3c,
	r[KEY_F4] = 0x3d,
	r[KEY_F5] = 0x3e,
	r[KEY_F6] = 0x3f,
	r[KEY_F7] = 0x40,
	r[KEY_F8] = 0x41,
	r[KEY_F9] = 0x42,
	r[KEY_F10] = 0x43,
	r[KEY_NUMLOCK] = 0x53,
	r[KEY_SCROLLLOCK] = 0x47,
	r[KEY_KP7] = 0x5f,
	r[KEY_KP8] = 0x60,
	r[KEY_KP9] = 0x61,
	r[KEY_KPMINUS] = 0x56,
	r[KEY_KP4] = 0x5c,
	r[KEY_KP5] = 0x5d,
	r[KEY_KP6] = 0x5e,
	r[KEY_KPPLUS] = 0x57,
	r[KEY_KP1] = 0x59,
	r[KEY_KP2] = 0x5a,
	r[KEY_KP3] = 0x5b,
	r[KEY_KP0] = 0x62,
	r[KEY_KPDOT] = 0x63,
	r[KEY_ZENKAKUHANKAKU] = 0x94,
	r[KEY_102ND] = 0x64,
	r[KEY_F11] = 0x44,
	r[KEY_F12] = 0x45,
	r[KEY_RO] = 0x87,
	r[KEY_KATAKANA] = 0x92,
	r[KEY_HIRAGANA] = 0x93,
	r[KEY_HENKAN] = 0x8a,
	r[KEY_KATAKANAHIRAGANA] = 0x88,
	r[KEY_MUHENKAN] = 0x8b,
	r[KEY_KPENTER] = 0x58,
	r[KEY_RIGHTCTRL] = 0xe4,
	r[KEY_KPSLASH] = 0x54,
	r[KEY_SYSRQ] = 0x46,
	r[KEY_RIGHTALT] = 0xe6,
	r[KEY_HOME] = 0x4a,
	r[KEY_UP] = 0x52,
	r[KEY_PAGEUP] = 0x4b,
	r[KEY_LEFT] = 0x50,
	r[KEY_RIGHT] = 0x4f,
	r[KEY_END] = 0x4d,
	r[KEY_DOWN] = 0x51,
	r[KEY_PAGEDOWN] = 0x4e,
	r[KEY_INSERT] = 0x49,
	r[KEY_DELETE] = 0x4c,
	r[KEY_MUTE] = 0x7f,
	r[KEY_VOLUMEDOWN] = 0x81,
	r[KEY_VOLUMEUP] = 0x80,
	r[KEY_POWER] = 0x66,
	r[KEY_KPEQUAL] = 0x67,
	r[KEY_KPPLUSMINUS] = 0xd7,
	r[KEY_PAUSE] = 0x48,
	r[KEY_KPCOMMA] = 0x85,
	r[KEY_HANGEUL] = 0x90,
	r[KEY_HANJA] = 0x91,
	r[KEY_YEN] = 0x89,
	r[KEY_LEFTMETA] = 0xe3,
	r[KEY_RIGHTMETA] = 0xe7,
	r[KEY_COMPOSE] = 0x65,
	r[KEY_AGAIN] = 0x79,
	r[KEY_UNDO] = 0x7a,
	r[KEY_FRONT] = 0x77,
	r[KEY_COPY] = 0x7c,
	r[KEY_OPEN] = 0x74,
	r[KEY_PASTE] = 0x7d,
	r[KEY_FIND] = 0x7e,
	r[KEY_CUT] = 0x7b,
	r[KEY_HELP] = 0x75,
	r[KEY_KPLEFTPAREN] = 0xb6,
	r[KEY_KPRIGHTPAREN] = 0xb7,
	r[KEY_F13] = 0x68,
	r[KEY_F14] = 0x69,
	r[KEY_F15] = 0x6a,
	r[KEY_F16] = 0x6b,
	r[KEY_F17] = 0x6c,
	r[KEY_F18] = 0x6d,
	r[KEY_F19] = 0x6e,
	r[KEY_F20] = 0x6f,
	r[KEY_F21] = 0x70,
	r[KEY_F22] = 0x71,
	r[KEY_F23] = 0x72,
	r[KEY_F24] = 0x73,
	void();
	return r;
}();

static uint8_t mods = 0;

static uint8_t keys[6] = {0};

struct vkbd {
	int fd = -1;

	vkbd() = default;
	vkbd(const vkbd&) = delete;
	vkbd& operator=(const vkbd&) = delete;
	~vkbd()
	{
		close(fd);
	}
};

struct hid_report {
	uint8_t hid_mods;
	uint8_t reserved;
	uint8_t hid_code[6];
};

static int create_virtual_keyboard()
{
	int fd = open("/dev/hidg0", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("open");
		exit(-1);
	}

	return fd;
}

static void send_hid_report(const struct vkbd *vkbd)
{
	struct hid_report report;

	for (int i = 0; i < 6; i++)
		report.hid_code[i] = keys[i];

	report.hid_mods = mods;

	xwrite(vkbd->fd, &report, sizeof(report));
}

static uint8_t get_modifier(int code)
{
	switch (code) {
	case KEY_LEFTSHIFT:
		return HID_SHIFT;
		break;
	case KEY_RIGHTSHIFT:
		return HID_RIGHTSHIFT;
		break;
	case KEY_LEFTCTRL:
		return HID_CTRL;
		break;
	case KEY_RIGHTCTRL:
		return HID_RIGHTCTRL;
		break;
	case KEY_LEFTALT:
		return HID_ALT;
		break;
	case KEY_RIGHTALT:
		return HID_ALT_GR;
		break;
	case KEY_LEFTMETA:
		return HID_SUPER;
		break;
	case KEY_RIGHTMETA:
		return HID_RIGHTSUPER;
		break;
	default:
		return 0;
		break;
	}
}

static int update_modifier_state(int code, int state)
{
	uint16_t mod = get_modifier(code);

	if (mod) {
		if (state)
			mods |= mod;
		else
			mods &= ~mod;
		return 0;
	}

	return -1;
}

static void update_key_state(uint16_t code, int state)
{
	if (code > KEY_MAX)
		return;

	int i;
	int set = 0;
	uint8_t hid_code = hid_table[code];

	for (i = 0; i < 6; i++) {
		if (keys[i] == hid_code) {
			set = 1;
			if (state == 0)
				keys[i] = 0;
		}
	}
	if (state && !set) {
		for (i = 0; i < 6; i++) {
			if (keys[i] == 0) {
				keys[i] = hid_code;
				break;
			}
		}
	}
}

struct vkbd* vkbd_init(const char *)
{
	static struct vkbd vkbd;
	if (vkbd.fd == -1)
		vkbd.fd = create_virtual_keyboard();

	return &vkbd;
}

void vkbd_mouse_move(struct vkbd* vkbd, int x, int y)
{
	fprintf(stderr, "usb-gadget: mouse support is not implemented\n");
}

void vkbd_mouse_move_abs(struct vkbd* vkbd, int x, int y)
{
	fprintf(stderr, "usb-gadget: mouse support is not implemented\n");
}

void vkbd_mouse_scroll(struct vkbd* vkbd, int x, int y)
{
	fprintf(stderr, "usb-gadget: mouse support is not implemented\n");
}

void vkbd_send_key(struct vkbd* vkbd, uint16_t code, int state)
{
	if (update_modifier_state(code, state) < 0)
		update_key_state(code, state);

	send_hid_report(vkbd);
}

void vkbd_flush(struct vkbd*)
{
}
