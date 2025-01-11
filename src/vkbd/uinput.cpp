/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <utility>

#ifdef __FreeBSD__
	#include <dev/evdev/uinput.h>
	#include <dev/evdev/input-event-codes.h>
#else
	#include <linux/uinput.h>
	#include <linux/input-event-codes.h>
#endif

#include "../keyd.h"

struct vkbd {
	int fd = -1;
	int pfd = -1;

	// Buffered wheel events
	int vwheel_buf = 0;
	int hwheel_buf = 0;

	vkbd() = default;
	vkbd(const vkbd&) = delete;
	vkbd& operator=(const vkbd&) = delete;
	~vkbd()
	{
		close(fd);
		close(pfd);
	}

	void send_kbd_event(int fd_type, uint16_t type, uint16_t code, int32_t value)
	{
		struct input_event ev[2]{};
		ev[0].type = type;
		ev[0].code = code;
		ev[0].value = value;
		ev[1].type = EV_SYN;

		// 0 = kbd, 1 = ptr, 2+ unimplemented
		if (fd_type == 0)
			xwrite(this->fd, ev, sizeof(ev));
		else if (fd_type == 1)
			xwrite(this->pfd, ev, sizeof(ev));
	}
};

static int create_virtual_keyboard(const char *name)
{
	int i;
	int ret;
	size_t code;
	struct uinput_user_dev udev = {};

	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		perror("open uinput");
		exit(-1);
	}

	if (ioctl(fd, UI_SET_EVBIT, EV_REP)) {
		perror("ioctl set_evbit");
		exit(-1);
	}

	if (ioctl(fd, UI_SET_EVBIT, EV_KEY)) {
		perror("ioctl set_evbit");
		exit(-1);
	}

	if (ioctl(fd, UI_SET_EVBIT, EV_LED)) {
		perror("ioctl set_evbit");
		exit(-1);
	}

	if (ioctl(fd, UI_SET_EVBIT, EV_SYN)) {
		perror("ioctl set_evbit");
		exit(-1);
	}

	for (code = 0; code < KEY_CNT; code++) {
		if (true) {
			if (ioctl(fd, UI_SET_KEYBIT, code)) {
				perror("ioctl set_keybit");
				exit(-1);
			}
		}
	}

	for (i = LED_NUML; i <= LED_MISC; i++)
		if (ioctl(fd, UI_SET_LEDBIT, i)) {
			perror("ioctl set_ledbit");
			exit(-1);
		}

	if (ioctl(fd, UI_SET_KEYBIT, KEY_ZOOM)) {
		perror("ioctl set_keybit");
		exit(-1);
	}

	udev.id.bustype = BUS_USB;
	udev.id.vendor = 0x0FAC;
	udev.id.product = 0x0ADE;

	snprintf(udev.name, sizeof(udev.name), "%s", name);

	/*
	 * We use this in favour of the newer UINPUT_DEV_SETUP
	 * ioctl in order to support older kernels.
	 *
	 * See: https://github.com/torvalds/linux/commit/052876f8e5aec887d22c4d06e54aa5531ffcec75
	 */
	ret = write(fd, &udev, sizeof udev);

	if (ret < 0) {
		fprintf(stderr, "failed to create uinput device\n");
		exit(-1);
	}

	if (ioctl(fd, UI_DEV_CREATE)) {
		perror("ioctl dev_create");
		exit(-1);
	}

	return fd;
}

static int create_virtual_pointer(const char *name)
{
	uint16_t code;
	struct uinput_user_dev udev = {};

	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		exit(-1);
	}

	ioctl(fd, UI_SET_EVBIT, EV_REL);
	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_SYN);

	ioctl(fd, UI_SET_ABSBIT, ABS_X);
	ioctl(fd, UI_SET_ABSBIT, ABS_Y);
	ioctl(fd, UI_SET_RELBIT, REL_X);
	ioctl(fd, UI_SET_RELBIT, REL_WHEEL);
	ioctl(fd, UI_SET_RELBIT, REL_HWHEEL);
	ioctl(fd, UI_SET_RELBIT, REL_Y);
	ioctl(fd, UI_SET_RELBIT, REL_Z);

	for (code = BTN_LEFT; code <= BTN_TASK; code++)
		ioctl(fd, UI_SET_KEYBIT, code);

	udev.id.bustype = BUS_USB;
	udev.id.vendor = 0x0FAC;
	udev.id.product = 0x1ADE;

	udev.absmax[ABS_X] = 1024;
	udev.absmax[ABS_Y] = 1024;

	snprintf(udev.name, sizeof(udev.name), "%s", name);

	if (write(fd, &udev, sizeof udev) < 0) {
		fprintf(stderr, "failed to create uinput device\n");
		exit(-1);
	}

	ioctl(fd, UI_DEV_CREATE);

	return fd;
}

static void write_key_event(struct vkbd *vkbd, uint16_t code, int state)
{
	int is_btn = 1;
	switch (code) {
		case KEYD_LEFT_MOUSE:
		case KEYD_MIDDLE_MOUSE:
		case KEYD_RIGHT_MOUSE:
		case KEYD_MOUSE_1:
		case KEYD_MOUSE_2:
		case KEYD_MOUSE_BACK:
		case KEYD_MOUSE_FORWARD:
			break;
		default:
			is_btn = 0;
			break;
	}

	if (is_btn) {
		/*
		 * Give key events preceding a mouse click
		 * a chance to propagate to avoid event
		 * order transposition. A bit kludegy,
		 * but better than waiting for all events
		 * to propagate and then checking them
		 * on re-entry.
		 *
		 * TODO: fixme (maybe)
		 */
		usleep(1000);
	}

	vkbd->send_kbd_event(is_btn, EV_KEY, code, state);
}

std::shared_ptr<vkbd> vkbd_init(const char *)
{
	auto vkbd = std::make_shared<struct vkbd>();
	vkbd->fd = create_virtual_keyboard(VKBD_NAME "keyboard");
	vkbd->pfd = create_virtual_pointer(VKBD_NAME "pointer");

	return vkbd;
}

void vkbd_mouse_move(struct vkbd *vkbd, int x, int y)
{
	if (x)
		vkbd->send_kbd_event(1, EV_REL, REL_X, x);
	if (y)
		vkbd->send_kbd_event(1, EV_REL, REL_Y, y);
}

void vkbd_mouse_scroll(struct vkbd* vkbd, int x, int y)
{
	vkbd->hwheel_buf += x;
	vkbd->vwheel_buf += y;
}

void vkbd_mouse_move_abs(struct vkbd* vkbd, int x, int y)
{
	if (x)
		vkbd->send_kbd_event(1, EV_ABS, ABS_X, x);
	if (y)
		vkbd->send_kbd_event(1, EV_ABS, ABS_Y, y);
}

void vkbd_send_key(struct vkbd* vkbd, uint16_t code, int state)
{
	dbg("output %s %s", KEY_NAME(code), state == 1 ? "down" : "up");

	if (KEYD_WHEELEVENT(code) && state) {
		// Buffer scroll events, a bit ugly but I hate to repeat constants
		(code & 2 ? vkbd->hwheel_buf : vkbd->vwheel_buf) += (code & 1 ? -1 : 1);
		return;
	}

	if (code > KEY_MAX)
		return;
	write_key_event(vkbd, code, state);
}

void vkbd_flush(struct vkbd* vkbd)
{
	// TODO: implement key buffering as well
	if (int y = std::exchange(vkbd->vwheel_buf, 0))
		vkbd->send_kbd_event(1, EV_REL, REL_WHEEL, y);
	if (int x = std::exchange(vkbd->hwheel_buf, 0))
		vkbd->send_kbd_event(1, EV_REL, REL_HWHEEL, x);
}
