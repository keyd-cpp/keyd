#include "keyd.h"

#define MAX_AUX_FDS 32

static int aux_fd = -1;
std::vector<device> device_table;

static void panic_check(uint16_t code, uint8_t pressed)
{
	static uint8_t enter, backspace, escape;
	switch (code) {
	case KEYD_ENTER:
		enter = pressed;
		break;
	case KEYD_BACKSPACE:
		backspace = pressed;
		break;
	case KEYD_ESC:
		escape = pressed;
		break;
	}

	if (backspace && enter && escape)
		die("panic sequence detected");
}

static long get_time_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1E3 + ts.tv_nsec / 1E6;
}

int evloop(int (*event_handler) (struct event *ev))
{
	size_t i;
	int timeout = 0;
	int monfd;

	struct pollfd pfds[130]{}; // TODO: fix hard limit but calling poll with too many descriptors is inefficient anyway

	struct event ev{};

	monfd = devmon_create();
	device_scan(device_table);

	for (i = 0; i < device_table.size(); i++) {
		ev.type = EV_DEV_ADD;
		ev.dev = &device_table[i];

		event_handler(&ev);
	}

	bool monitor = true;
	for (i = 0; i < device_table.size(); i++) {
		if (device_table[i].grabbed) {
			monitor = false;
			break;
		}
	}

	pfds[0].fd = monfd;
	pfds[0].events = POLLIN;
	pfds[1].fd = aux_fd;
	pfds[1].events = POLLIN;
	auto pfdsd = pfds + 2;

	while (1) {
		int removed = 0;

		int start_time;
		int elapsed;

		for (i = 0; i < device_table.size(); i++) {
			if (i == std::size(pfds) - (pfdsd - pfds)) {
				break;
			}
			pfdsd[i].fd = device_table[i].fd;
			pfdsd[i].events = 0;
			if (monitor || device_table[i].grabbed)
				pfdsd[i].events = POLLIN;
			else if (device_table[i].capabilities & CAP_KEYBOARD && device_table[i].is_virtual)
				pfdsd[i].events = POLLIN;
		}

		start_time = get_time_ms();
		poll(pfds, i + (pfdsd - pfds), timeout > 0 ? timeout : -1);
		ev.timestamp = get_time_ms();
		elapsed = ev.timestamp - start_time;

		if (timeout > 0 && elapsed >= timeout) {
			ev.type = EV_TIMEOUT;
			ev.dev = NULL;
			ev.devev = NULL;
			timeout = event_handler(&ev);
		} else {
			timeout -= elapsed;
		}

		// Count backwards
		for (; i + 1; i--) {
			if (pfdsd[i].revents) {
				struct device_event *devev = nullptr;

				while ((pfdsd[i].revents & (POLLERR | POLLHUP)) || (devev = device_read_event(&device_table[i]))) {
					if (!devev || devev->type == DEV_REMOVED) {
						ev.type = EV_DEV_REMOVE;
						ev.dev = &device_table[i];

						timeout = event_handler(&ev);

						close(device_table[i].fd);
						device_table[i].fd = -1;
						removed = 1;
						break;
					} else {
						//Handle device event
						panic_check(devev->code, devev->pressed);

						ev.type = EV_DEV_EVENT;
						ev.devev = devev;
						ev.dev = &device_table[i];

						timeout = event_handler(&ev);
					}
				}
			}
		}

		{
			if (auto events = pfds[1].revents) {
				ev.type = events & POLLERR ? EV_FD_ERR : EV_FD_ACTIVITY;
				ev.fd = aux_fd;

				timeout = event_handler(&ev);
			}
		}


		if (pfds[0].revents) {
			struct device dev;

			while (devmon_read_device(monfd, &dev) == 0) {
				if (device_table.size() >= std::size(pfds) - 2) {
					err("Too many devices, some of them will be ignored.");
				}
				device_table.emplace_back(std::move(dev));

				ev.type = EV_DEV_ADD;
				ev.dev = &device_table.back();

				timeout = event_handler(&ev);
			}
		}

		if (removed) {
			std::erase_if(device_table, [](device& dev) {
				return dev.fd < 0;
			});
		}

	}

	return 0;
}

void evloop_add_fd(int fd)
{
	assert(aux_fd < 0);
	aux_fd = fd;
}
