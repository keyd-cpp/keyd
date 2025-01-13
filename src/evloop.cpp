#include "keyd.h"

static int aux_fd = -1;

// Expected to be initialized as zeros
// Expected to terminate if fd 0 or -1
std::array<device, 128> device_table{};

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

static int64_t get_time_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000'000;
}

int evloop(int (*event_handler) (struct event *ev))
{
	size_t n_dev;
	int timeout = 0;
	int monfd;

	struct pollfd pfds[device_table.size() + 3]{};

	struct event ev{};

	monfd = devmon_create();
	n_dev = device_scan(device_table);

	for (size_t i = 0; i < n_dev; i++) {
		ev.type = EV_DEV_ADD;
		ev.dev = &device_table[i];

		event_handler(&ev);
	}

	bool monitor = true;
	for (size_t i = 0; i < n_dev; i++) {
		if (device_table[i].grabbed) {
			monitor = false;
			break;
		}
	}

	pfds[0].fd = monfd;
	pfds[0].events = POLLIN;
	pfds[1].fd = aux_fd;
	pfds[1].events = POLLIN;
	pfds[2].fd = STDOUT_FILENO;
	pfds[2].events = 0;
	auto pfdsd = pfds + 3;

	while (1) {
		int removed = 0;

		int64_t start_time;
		int64_t elapsed;

		for (size_t i = 0; i < n_dev; i++) {
			pfdsd[i].fd = device_table[i].fd;
			pfdsd[i].events = 0;
			if (monitor || device_table[i].grabbed)
				pfdsd[i].events = POLLIN;
			else if (device_table[i].capabilities & CAP_KEYBOARD && device_table[i].is_virtual)
				pfdsd[i].events = POLLIN;
		}

		start_time = get_time_ms();
		poll(pfds, n_dev + (pfdsd - pfds), timeout > 0 ? timeout : -1);
		ev.timestamp = get_time_ms();
		elapsed = ev.timestamp - start_time;

		if (pfds[2].revents) {
			// Handle pipe closure
			break;
		}

		if (timeout > 0 && elapsed >= timeout) {
			ev.type = EV_TIMEOUT;
			ev.dev = NULL;
			ev.devev = NULL;
			timeout = event_handler(&ev);
		} else {
			timeout -= elapsed;
		}

		for (size_t i = 0; i < n_dev; i++) {
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
				if (n_dev >= device_table.size()) {
					keyd_log("Too many devices, ignoring.");
					close(dev.fd);
					break;
				}
				device_table[n_dev] = std::move(dev);

				ev.type = EV_DEV_ADD;
				ev.dev = &device_table[n_dev];

				timeout = event_handler(&ev);
				n_dev++;
			}
		}

		if (removed) {
			// Maintain contiguous list of devices
			auto end = std::remove_if(device_table.begin(), device_table.begin() + n_dev, [](device& dev) {
				return dev.fd <= 0;
			});
			if (end < device_table.begin() + n_dev) {
				end->fd = -1;
				n_dev = end - device_table.data();
			}
		}
	}

	return 0;
}

void evloop_add_fd(int fd)
{
	assert(aux_fd < 0);
	aux_fd = fd;
}
