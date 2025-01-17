/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include "keyd.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <numeric>
#include "concat.hpp"

/*
 * Abstract away evdev and inotify.
 *
 * We could make this cleaner by creating a single file descriptor via epoll
 * but this would break FreeBSD compatibility without a dedicated kqueue
 * implementation. A thread based approach was also considered, but
 * inter-thread communication adds too much overhead (~100us).
 *
 * Overview:
 *
 * A 'devmon' is a file descriptor which can be created with devmon_create()
 * and subsequently monitored for new devices read with devmon_read_device().
 *
 * A 'device' always corresponds to a keyboard or mouse from which activity can
 * be monitored with device->fd and events subsequently read using
 * device_read_event().
 *
 * If the event returned by device_read_event() is of type DEV_REMOVED then the
 * corresponding device should be considered invalid by the caller.
 */

static uint8_t resolve_device_capabilities(int fd, uint32_t *num_keys, uint8_t *relmask, uint8_t *absmask)
{
	const uint32_t keyboard_mask = 1<<KEY_1  | 1<<KEY_2 | 1<<KEY_3 |
					1<<KEY_4 | 1<<KEY_5 | 1<<KEY_6 |
					1<<KEY_7 | 1<<KEY_8 | 1<<KEY_9 |
					1<<KEY_0 | 1<<KEY_Q | 1<<KEY_W |
					1<<KEY_E | 1<<KEY_R | 1<<KEY_T |
					1<<KEY_Y;

	size_t i;
	uint32_t mask[BTN_LEFT/32+1] = {0};
	uint8_t capabilities = 0;
	int has_brightness_key, has_volume_key;

	if (ioctl(fd, EVIOCGBIT(EV_KEY, (BTN_LEFT/32+1)*4), mask) < 0) {
		perror("ioctl: ev_key");
		return 0;
	}

	if (ioctl(fd, EVIOCGBIT(EV_REL, 1), relmask) < 0) {
		perror("ioctl: ev_rel");
		return 0;
	}

	if (ioctl(fd, EVIOCGBIT(EV_ABS, 1), absmask) < 0) {
		perror("ioctl: ev_abs");
		return 0;
	}

	uint8_t led_caps = 0;
	if (ioctl(fd, EVIOCGBIT(EV_LED, 1), &led_caps) < 0) {
		perror("ioctl: EV_LED");
		return 0;
	}

	*num_keys = 0;
	for (i = 0; i < sizeof(mask)/sizeof(mask[0]); i++)
		*num_keys += __builtin_popcount(mask[i]);

	if (*relmask || *absmask)
		capabilities |= CAP_MOUSE;

	if (*absmask)
		capabilities |= CAP_MOUSE_ABS;

	if (led_caps)
		capabilities |= CAP_LEDS;

	/*
	 * If the device can emit KEY_BRIGHTNESSUP or KEY_VOLUMEUP, we treat it as a keyboard.
	 *
	 * This is mainly to accommodate laptops with brightness/volume buttons which create
	 * a different device node from the main keyboard for some hotkeys.
	 *
	 * NOTE: This will subsume anything that can emit a brightness key and may produce
	 * false positives which need to be explcitly excluded by the user if they use
	 * the wildcard id.
	 */
	has_brightness_key = mask[KEY_BRIGHTNESSUP/32] & (1 << (KEY_BRIGHTNESSUP % 32));
	has_volume_key = mask[KEY_VOLUMEUP/32] & (1 << (KEY_VOLUMEUP % 32));

	if (((mask[0] & keyboard_mask) == keyboard_mask) || has_brightness_key || has_volume_key)
		capabilities |= CAP_KEYBOARD;

	return capabilities;
}

uint32_t generate_uid(uint32_t num_keys, uint8_t absmask, uint8_t relmask, const char *name)
{
	uint32_t hash = 5183;

	//djb2 hash
	hash = hash*33 + (uint8_t)(num_keys >> 24);
	hash = hash*33 + (uint8_t)(num_keys >> 16);
	hash = hash*33 + (uint8_t)(num_keys >> 8);
	hash = hash*33 + (uint8_t)(num_keys);
	hash = hash*33 + absmask;
	hash = hash*33 + relmask;

	while (*name) {
		hash = hash*33 + *name;
		name++;
	}

	return hash;
}

static int device_init(struct device *dev)
{
	int fd;
	int capabilities;
	uint32_t num_keys;
	uint8_t relmask;
	uint8_t absmask;
	struct input_absinfo absinfo;

	auto path = concat("/dev/input/event", dev->num);
	if ((fd = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC, 0600)) < 0) {
		keyd_log("failed to open %s\n", path.c_str());
		return -1;
	}

	capabilities = resolve_device_capabilities(fd, &num_keys, &relmask, &absmask);

	memset(dev->name, 0, sizeof(dev->name));
	if (ioctl(fd, EVIOCGNAME(sizeof(dev->name) - 1), dev->name) == -1) {
		keyd_log("ERROR: could not fetch device name of /dev/input/event%u\n", dev->num);
		return -1;
	}

	if (capabilities & CAP_MOUSE_ABS) {
		if (ioctl(fd, EVIOCGABS(ABS_X), &absinfo) < 0) {
			perror("ioctl");
			return -1;
		}

		dev->_minx = absinfo.minimum;
		dev->_maxx = absinfo.maximum;

		if (ioctl(fd, EVIOCGABS(ABS_Y), &absinfo) < 0) {
			perror("ioctl");
			return -1;
		}

		dev->_miny = absinfo.minimum;
		dev->_maxy = absinfo.maximum;
	}

	dbg2("capabilities of %s (%s): %x", path.c_str(), dev->name, capabilities);

	if (capabilities) {
		struct input_id info;

		if (ioctl(fd, EVIOCGID, &info) == -1) {
			perror("ioctl EVIOCGID");
			return -1;
		}

		/*
		 * Attempt to generate a reproducible unique identifier for each device.
		 * The product and vendor ids are insufficient to identify some devices since
		 * they can create multiple device nodes with different capabilities. Thus
		 * we factor in the device name and capabilities of the resultant evdev node
		 * to further distinguish between input devices. These should be regarded as
		 * opaque identifiers by the user.
		 */
		snprintf(dev->id, sizeof(dev->id) - 1, "%04x:%04x:%08x", info.vendor, info.product, generate_uid(num_keys, absmask, relmask, dev->name));

		dev->fd = fd;
		dev->capabilities = capabilities;
		dev->data = NULL;
		dev->grabbed = 0;

		dev->is_virtual = std::string_view(dev->name).starts_with(VKBD_NAME);
		return 0;
	} else {
		close(fd);
		return -1;
	}

	return -1;
}

size_t device_scan(std::array<device, 128>& devices)
{
	DIR *dh = opendir("/dev/input/");
	if (!dh) {
		perror("opendir /dev/input");
		exit(-1);
	}

	size_t n = 0;
	while (struct dirent* ent = readdir(dh)) {
		if (ent->d_type != DT_DIR && !memcmp(ent->d_name, "event", 5)) {
			if (n >= devices.size()) {
				keyd_log("Too many devices, ignoring.\n");
				break;
			}
			auto& dev = devices[n];
			dev.num = atoi(ent->d_name + 5);
			if (device_init(&dev) >= 0)
				n++;
		}
	}

	closedir(dh);
	return n;
}

/*
 * NOTE: Only a single devmon fd may exist. Implementing this properly
 * would involve bookkeeping state for each fd, but this is
 * unnecessary for our use.
 */
int devmon_create()
{
	static int init = 0;
	assert(!init);
	init = 1;

	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0) {
		perror("inotify");
		exit(-1);
	}

	int wd = inotify_add_watch(fd, "/dev/input/", IN_CREATE);
	if (wd < 0) {
		perror("inotify");
		exit(-1);
	}

	return fd;
}

/*
 * A non blocking call which returns any devices available on the provided
 * monitor descriptor. Returns 0 on success.
 */
int devmon_read_device(int fd, struct device *dev)
{
	static char buf[4096];
	static int buf_sz = 0;
	static char *ptr = buf;

	while (1) {
		struct inotify_event *ev;

		if (ptr >= (buf+buf_sz)) {
			ptr = buf;
			buf_sz = read(fd, buf, sizeof(buf));
			if (buf_sz == -1) {
				buf_sz = 0;
				return -1;
			}
		}

		ev = (struct inotify_event*)ptr;
		ptr += sizeof(struct inotify_event) + ev->len;

		if (strncmp(ev->name, "event", 5))
			continue;

		dev->num = atoi(ev->name + 5);
		if (!device_init(dev))
			return 0;
	}
}

int device_grab(struct device *dev)
{
	struct input_event ev;
	uint8_t state[KEY_MAX / 8 + 1]{};
	int pending_release = 0;

	if (dev->grabbed)
		return 0;

	/*
	 * await neutral key state to ensure any residual
	 * key up events propagate.
	 */

	for (size_t i = 0; i < 1000; i++) {
		if (ioctl(dev->fd, EVIOCGKEY(sizeof state), state) < 0) {
			perror("ioctl EVIOCGKEY");
			return -1;
		}

		pending_release = std::accumulate(+state, std::end(state), 0);
		if (!pending_release)
			break;
		usleep(10'000);
	}

	if (pending_release) {
		for (size_t i = 0; i <= KEY_MAX; i++) {
			if ((state[i / 8] >> (i % 8)) & 0x1)
				printf("Waiting for key %s...\n", KEY_NAME(i));
		}

		//Allow the key up events to propagate before
		//grabbing the device.
		usleep(50'000);
	}

	if (dev->capabilities & CAP_LEDS && ioctl(dev->fd, EVIOCGLED(LED_CNT), dev->led_state) < 0) {
		perror("EVIOCGLED");
		return -1;
	}

	if (ioctl(dev->fd, EVIOCGRAB, (void *) 1) < 0) {
		perror("EVIOCGRAB");
		return -1;
	}

	/* drain any input events before the grab (assumes NONBLOCK is set on the fd) */
	while (read(dev->fd, &ev, sizeof(ev)) > 0) {
	}

	dev->grabbed = 1;
	return 0;
}

int device_ungrab(struct device *dev)
{
	if (!dev->grabbed)
		return 0;

	if (!ioctl(dev->fd, EVIOCGRAB, (void *) 0)) {
		if (dev->capabilities & CAP_LEDS) {
			for (int i = 0; i < LED_CNT; i++) {
				device_set_led(dev, dev->led_state[i], i);
			}
		}

		dev->grabbed = 0;
		return 0;
	} else {
		return -1;
	}
}

/*
 * Read a device event from the given device or return
 * NULL if none are available (may happen in the
 * case of a spurious wakeup).
 */
struct device_event *device_read_event(struct device *dev)
{
	struct input_event ev;
	static struct device_event devev;

	assert(dev->fd != -1);

	if (read(dev->fd, &ev, sizeof(ev)) < 0) {
		if (errno == EAGAIN) {
			return NULL;
		} else {
			devev.type = DEV_REMOVED;
			return &devev;
		}
	}

	switch (ev.type) {
	case EV_REL:
		switch (ev.code) {
		case REL_WHEEL:
			devev.type = DEV_MOUSE_SCROLL;
			devev.y = ev.value;
			devev.x = 0;

			break;
		case REL_HWHEEL:
			devev.type = DEV_MOUSE_SCROLL;
			devev.y = 0;
			devev.x = ev.value;

			break;
		case REL_X:
			devev.type = DEV_MOUSE_MOVE;
			devev.x = ev.value;
			devev.y = 0;

			break;
		case REL_Y:
			devev.type = DEV_MOUSE_MOVE;
			devev.y = ev.value;
			devev.x = 0;

			break;
//		case REL_WHEEL_HI_RES:
//			/* TODO: implement me */
//			return NULL;
//		case REL_HWHEEL_HI_RES:
//			/* TODO: implement me */
//			return NULL;
		default:
			dbg("Unrecognized EV_REL code: %d\n", ev.code);
			return NULL;
		}

		break;
	case EV_ABS:
		switch (ev.code) {
		case ABS_X:
			devev.type = DEV_MOUSE_MOVE_ABS;
			devev.x = (ev.value * 1024) / (dev->_maxx - dev->_minx);
			devev.y = 0;

			break;
		case ABS_Y:
			devev.type = DEV_MOUSE_MOVE_ABS;
			devev.y = (ev.value * 1024) / (dev->_maxy - dev->_miny);
			devev.x = 0;

			break;
		default:
			dbg("Unrecognized EV_ABS code: %x", ev.code);
			break;
		}

		break;
	case EV_KEY:
		/* Ignore repeat events. */
		if (ev.value == 2)
			return NULL;

		devev.type = DEV_KEY;
		devev.code = ev.code;
		devev.pressed = ev.value;

		dbg2("key %s %s", KEY_NAME(devev.code), devev.pressed ? "down" : "up");

		break;
	case EV_LED:
		devev.type = DEV_LED;
		devev.code = ev.code;
		devev.pressed = ev.value;

		break;
	default:
		if (ev.type)
			dbg2("unrecognized evdev event type: %d %d %d", ev.type, ev.code, ev.value);
		return NULL;
	}

	return &devev;
}

void device_set_led(const struct device *dev, uint8_t led, int state)
{
	if (led > LED_MAX || !(dev->capabilities & CAP_LEDS))
		return;

	struct input_event ev[2]{};
	ev[0].type = EV_LED;
	ev[0].code = led;
	ev[0].value = state;
	ev[1].type = EV_SYN;

	xwrite(dev->fd, &ev, sizeof ev);
}
