/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#ifdef __FreeBSD__
	#include <dev/evdev/uinput.h>
	#include <dev/evdev/input-event-codes.h>
#else
	#include <linux/uinput.h>
	#include <linux/input-event-codes.h>
#endif

#define REPEAT_INTERVAL 40
#define REPEAT_TIMEOUT 200

#include "../keyd.h"

struct vkbd {
	int fd;
	int pfd;

	vkbd() = default;
	vkbd(const vkbd&) = delete;
	vkbd& operator=(const vkbd&) = delete;
	~vkbd()
	{
		close(fd);
		close(pfd);
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

static void write_key_event(const struct vkbd *vkbd, uint16_t code, int state)
{
	struct input_event ev;
	int fd;
	int is_btn;

	fd = vkbd->fd;

	ev.type = EV_KEY;
	ev.code = code;

	is_btn = 1;
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

	/*
	 * Send all buttons through the virtual pointer
	 * to prevent X from identifying the virtual
	 * keyboard as a mouse.
	 */
	if (is_btn) {
		fd = vkbd->pfd;

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

	ev.value = state;

	ev.time.tv_sec = 0;
	ev.time.tv_usec = 0;

	xwrite(fd, &ev, sizeof(ev));

	ev.type = EV_SYN;
	ev.code = 0;
	ev.value = 0;


	xwrite(fd, &ev, sizeof(ev));
}

std::shared_ptr<vkbd> vkbd_init(const char *)
{
	auto vkbd = std::make_shared<struct vkbd>();
	vkbd->fd = create_virtual_keyboard(VKBD_NAME "keyboard");
	vkbd->pfd = create_virtual_pointer(VKBD_NAME "pointer");

	return vkbd;
}

void vkbd_mouse_move(const struct vkbd *vkbd, int x, int y)
{
	struct input_event ev;

	if (x) {
		ev.type = EV_REL;
		ev.code = REL_X;
		ev.value = x;

		ev.time.tv_sec = 0;
		ev.time.tv_usec = 0;

		xwrite(vkbd->pfd, &ev, sizeof(ev));
	}

	if (y) {
		ev.type = EV_REL;
		ev.code = REL_Y;
		ev.value = y;

		ev.time.tv_sec = 0;
		ev.time.tv_usec = 0;

		xwrite(vkbd->pfd, &ev, sizeof(ev));
	}

	ev.type = EV_SYN;
	ev.code = 0;
	ev.value = 0;

	xwrite(vkbd->pfd, &ev, sizeof(ev));
}

void vkbd_mouse_scroll(const struct vkbd *vkbd, int x, int y)
{
	struct input_event ev;

	ev.type = EV_REL;
	ev.code = REL_WHEEL;
	ev.value = y;

	ev.time.tv_sec = 0;
	ev.time.tv_usec = 0;

	xwrite(vkbd->pfd, &ev, sizeof(ev));

	ev.type = EV_REL;
	ev.code = REL_HWHEEL;
	ev.value = x;

	ev.time.tv_sec = 0;
	ev.time.tv_usec = 0;

	xwrite(vkbd->pfd, &ev, sizeof(ev));

	ev.type = EV_SYN;
	ev.code = 0;
	ev.value = 0;

	xwrite(vkbd->pfd, &ev, sizeof(ev));
}

void vkbd_mouse_move_abs(const struct vkbd *vkbd, int x, int y)
{
	struct input_event ev;

	if (x) {
		ev.type = EV_ABS;
		ev.code = ABS_X;
		ev.value = x;

		ev.time.tv_sec = 0;
		ev.time.tv_usec = 0;

		xwrite(vkbd->pfd, &ev, sizeof(ev));
	}

	if (y) {
		ev.type = EV_ABS;
		ev.code = ABS_Y;
		ev.value = y;

		ev.time.tv_sec = 0;
		ev.time.tv_usec = 0;

		xwrite(vkbd->pfd, &ev, sizeof(ev));
	}

	ev.type = EV_SYN;
	ev.code = 0;
	ev.value = 0;

	xwrite(vkbd->pfd, &ev, sizeof(ev));
}

void vkbd_send_key(const struct vkbd *vkbd, uint16_t code, int state)
{
	dbg("output %s %s", KEY_NAME(code), state == 1 ? "down" : "up");

	if (code > KEY_MAX)
		return;
	write_key_event(vkbd, code, state);
}
