#include "keyd.h"
#include "log.h"
#include <memory>
#include <bitset>
#include <utility>

#ifndef CONFIG_DIR
#define CONFIG_DIR ""
#endif

static int ipcfd = -1;
static std::shared_ptr<struct vkbd> vkbd;
static std::vector<std::unique_ptr<keyboard>> configs;
extern std::array<device, 128> device_table;

static std::bitset<KEY_CNT> keystate{};

struct listener
{
	listener() = default;
	explicit listener(int fd)
		: fd(fd)
	{
	}

	listener(const listener&) = delete;
	listener& operator=(const listener&) = delete;

	listener(listener&& r)
	{
		if (fd != -1)
			close(fd);
		std::swap(fd, r.fd);
		r.fd = -1;
	}

	listener& operator=(listener&& r)
	{
		std::swap(fd, r.fd);
		return *this;
	}

	~listener()
	{
		if (fd != -1)
			close(fd);
	}

	operator int() const
	{
		return fd;
	}

private:
	int fd = -1;
};

static std::array<listener, 32> listeners;

static struct keyboard *active_kbd = NULL;

static void cleanup()
{
	for (auto& dev : device_table) {
		if (dev.fd > 0) {
			close(dev.fd);
			dev.fd = -1;
		}
	}
}

static void clear_vkbd()
{
	for (size_t i = 0; i < keystate.size(); i++) {
		if (keystate[i]) {
			vkbd_send_key(vkbd.get(), i, 0);
			keystate[i] = 0;
		}
	}

	vkbd_flush(vkbd.get());
}

static void send_key(uint16_t code, uint8_t state)
{
	if (code < keystate.size())
		keystate[code] = state;
	vkbd_send_key(vkbd.get(), code, state);
}

static void add_listener(::listener con)
{
	struct timeval tv;

	/*
	 * In order to avoid blocking the main event loop, allow up to 50ms for
	 * slow clients to relieve back pressure before dropping them.
	 */
	tv.tv_usec = 50000;
	tv.tv_sec = 0;

	setsockopt(con, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

	if (active_kbd) {
		struct config *config = &active_kbd->config;
		dprintf(con, "/%s\n", config->layers[active_kbd->layout].name.c_str());

		for (size_t i = 0; i < config->layers.size(); i++) {
			if (active_kbd->layer_state[i].active()) {
				struct layer *layer = &config->layers[i];

				if (i != size_t(active_kbd->layout)) {
					if (dprintf(con, "+%s\n", layer->name.c_str()) < 0)
						return;
				}
			}
		}
	}

	for (auto& lis : listeners) {
		if (!lis) {
			lis = std::move(con);
			return;
		}
	}

	keyd_log("Too many listeners, ignoring.");
}

static void activate_leds(const struct keyboard *kbd)
{
	int ind = kbd->config.layer_indicator;
	if (ind > LED_MAX)
		return;

	int active_layers = 0;

	for (size_t i = 1; i < kbd->config.layers.size(); i++)
		if (i != size_t(kbd->layout) && kbd->layer_state[i].active()) {
			active_layers = 1;
			break;
		}

	for (size_t i = 0; i < device_table.size(); i++) {
		if (device_table[i].fd <= 0)
			break;
		if (device_table[i].data == kbd && (device_table[i].capabilities & CAP_LEDS)) {
			if (std::exchange(device_table[i].led_state[ind], active_layers) == active_layers)
				continue;
			device_set_led(&device_table[i], ind, active_layers);
		}
	}
}

static void restore_leds()
{
	for (size_t i = 0; i < device_table.size(); i++) {
		struct device* dev = &device_table[i];
		if (dev->fd <= 0)
			break;
		if (dev->grabbed && dev->data && (dev->capabilities & CAP_LEDS)) {
			for (int j = 0; j < LED_CNT; j++) {
				device_set_led(dev, dev->led_state[j], j);
			}
		}
	}
}

static void on_layer_change(const struct keyboard *kbd, struct layer *layer, uint8_t state)
{
	if (kbd->config.layer_indicator) {
		activate_leds(kbd);
	}

	char c = '/';
	if (kbd->layout != (layer - kbd->config.layers.data()))
		c = state ? '+' : '-';

	for (auto& listener : listeners) {
		if (listener < 0)
			continue;
		if (layer->name[0]) {
			if (dprintf(listener, "%c%s\n", c, layer->name.c_str()) < 0) {
				listener = {};
				continue;
			}
		}
		for (auto idx : *layer) {
			if (dprintf(listener, "%c%s\n", c, kbd->config.layers[idx].name.c_str()) < 0) {
				listener = {};
				break;
			}
		}
	}
}

static void load_configs()
{
	DIR *dh = opendir(CONFIG_DIR);

	if (!dh) {
		perror("opendir");
		exit(-1);
	}

	configs.clear();

	while (struct dirent* dirent = readdir(dh)) {
		if (dirent->d_type == DT_DIR)
			continue;

		auto name = concat(CONFIG_DIR "/", dirent->d_name);
		if (name.get().ends_with(".conf")) {
			keyd_log("CONFIG: parsing b{%s}\n", name.c_str());

			auto kbd = std::make_unique<keyboard>();
			if (config_parse(&kbd->config, name.c_str())) {
				kbd->output = {
					.send_key = send_key,
					.on_layer_change = on_layer_change,
				};
				kbd = new_keyboard(std::move(kbd));
				kbd->original_config.reserve(2);
				kbd->original_config.emplace_back(kbd->config);

				configs.emplace_back(std::move(kbd));
			} else {
				keyd_log("DEVICE: y{WARNING} failed to parse %s\n", name.c_str());
			}

		}
	}

	closedir(dh);
}

static std::unique_ptr<keyboard>* lookup_config_ent(const char *id, uint8_t flags)
{
	std::unique_ptr<keyboard>* match = nullptr;
	int rank = 0;

	for (auto& kbd : configs) {
		int r = config_check_match(&kbd->config, id, flags);

		if (r > rank) {
			match = &kbd;
			rank = r;
		}
	}

	return match;
}

static void manage_device(struct device *dev)
{
	uint8_t flags = 0;

	if (dev->is_virtual)
		return;

	if (dev->capabilities & CAP_KEYBOARD)
		flags |= ID_KEYBOARD;
	if (dev->capabilities & (CAP_MOUSE|CAP_MOUSE_ABS))
		flags |= ID_MOUSE;
	if (dev->capabilities & CAP_MOUSE_ABS)
		flags |= ID_ABS_PTR;

	if (auto ent = lookup_config_ent(dev->id, flags)) {
		if (device_grab(dev)) {
			keyd_log("DEVICE: y{WARNING} Failed to grab /dev/input/%u\n", dev->num);
			dev->data = NULL;
			return;
		}

		keyd_log("DEVICE: g{match}    %s  %s\t(%s)\n",
			  dev->id, ent->get()->config.pathstr.c_str(), dev->name);

		dev->data = ent->get();
		if (dev->capabilities & CAP_LEDS)
			device_set_led(dev, ent->get()->config.layer_indicator, 0);
	} else {
		dev->data = NULL;
		device_ungrab(dev);
		keyd_log("DEVICE: r{ignoring} %s  (%s)\n", dev->id, dev->name);
	}
}

static void reload(const std::shared_ptr<env_pack>& env)
{
	restore_leds();
	load_configs();

	for (auto& dev : device_table) {
		if (dev.fd <= 0)
			break;
		manage_device(&dev);
	}

	clear_vkbd();

	if (env && env->uid >= 1000) {
		// Load user bindings (may be not loaded when executed as root)
		std::string buf;
		if (auto v = env->getenv("XDG_CONFIG_HOME"))
			buf.assign(v) += "/";
		else if (auto v = env->getenv("HOME"))
			buf.assign(v) += "/.config/";
		else
			buf.clear();
		buf += "keyd/bindings.conf";
		file_mapper file(open(buf.c_str(), O_RDONLY));
		if (!file) {
			keyd_log("Unable to open %s\n", buf.c_str());
			return;
		}

		for (auto& kbd : configs) {
			kbd->config.cmd_env = env;
			for (auto str : split_char<'\n'>(file.view())) {
				if (str.empty())
					continue;
				if (!kbd_eval(kbd.get(), str))
					keyd_log("Invalid binding: %.*s\n", (int)str.size(), str.data());
			}
			kbd->original_config.emplace_back(kbd->config);
		}
	}
}

static void send_success(int con)
{
	struct ipc_message msg = {};

	msg.type = IPC_SUCCESS;
	msg.sz = 0;

	xwrite(con, &msg, sizeof msg);
}

static void send_fail(int con, const char *fmt, ...)
{
	struct ipc_message msg = {};
	va_list args;

	va_start(args, fmt);

	msg.type = IPC_FAIL;
	msg.sz = vsnprintf(msg.data, sizeof(msg.data), fmt, args);

	xwrite(con, &msg, sizeof msg);

	va_end(args);
}

static bool handle_message(::listener& con, struct config* config, const std::shared_ptr<env_pack>& env)
{
	struct ipc_message msg;
	std::string input_buf;
	if (!xread(con, &msg, sizeof(msg))) {
		// Disconnected
		return false;
	}

	if (msg.sz >= sizeof(msg.data)) {
		send_fail(con, "maximum message size exceeded");
		return false;
	}
	msg.data[msg.sz] = 0;

	if (msg.timeout > 1000000) {
		send_fail(con, "timeout cannot exceed 1000 ms");
		return false;
	}

	::macro macro;
	switch (msg.type) {
		int success;

	case IPC_INPUT:
	case IPC_MACRO:
		while (msg.sz && msg.data[msg.sz-1] == '\n')
			msg.data[--msg.sz] = 0;

		if (msg.type == IPC_INPUT)
			input_buf += "type(";
		input_buf += msg.data;
		if (msg.type == IPC_INPUT)
			input_buf += ")";

		if (macro_parse(input_buf, macro, config)) {
			send_fail(con, "%s", errstr);
			break;
		}

		macro_execute(send_key, macro, msg.timeout, config);
		send_success(con);

		break;
	case IPC_RELOAD:
		reload(std::move(env));
		send_success(con);
		break;
	case IPC_LAYER_LISTEN:
		add_listener(std::move(con));
		return false;
	case IPC_BIND:
		success = 0;

		if (msg.sz == sizeof(msg.data)) {
			send_fail(con, "bind expression size exceeded");
			break;
		}

		msg.data[msg.sz] = 0;

		for (auto& kbd : configs) {
			if (kbd->config.cmd_env && config->cmd_env && kbd->config.cmd_env != config->cmd_env) {
				// Assign only if objects differ
				if (*kbd->config.cmd_env != *config->cmd_env)
					kbd->config.cmd_env = config->cmd_env;
			} else {
				kbd->config.cmd_env = config->cmd_env;
			}
			success |= kbd_eval(kbd.get(), msg.data);
		}

		if (success)
			send_success(con);
		else
			send_fail(con, "%s", errstr);

		// Repeat
		return true;
	default:
		send_fail(con, "Unknown command");
		break;
	}

	return false;
}

[[gnu::noinline]] static void handle_client(int fd)
{
	if (fd < 0) {
		perror("accept");
		exit(-1);
	}
	::listener con(fd);
	socklen_t ucred_len = sizeof(struct ucred);
	struct ucred cred{};
	if (getsockopt(con, SOL_SOCKET, SO_PEERCRED, &cred, &ucred_len) < 0)
		return;

	::config ephemeral_config;
	if (getuid() < 1000)
	{
		// Copy initial environment variables from caller process
		std::vector<char> buf = file_reader(open(concat("/proc/", cred.pid, "/environ").c_str(), O_RDONLY), 8192, [] {
			perror("environ");
		});
		if (!buf.empty()) {
			size_t count = std::count(buf.begin(), buf.end(), 0);
			auto env = std::make_unique<const char*[]>(count + 1);
			auto ptr = env.get();
			for (auto str : split_char<0>({buf.data(), buf.size() - 1}))
				*ptr++ = str.data();
			*ptr = nullptr;
			ephemeral_config.cmd_env = std::make_shared<env_pack>(::env_pack{
				.buf = std::move(buf),
				.env = std::move(env),
				.uid = cred.uid,
				.gid = cred.gid,
			});
		}
	}

	size_t msg_count = 1;
	while (handle_message(con, &ephemeral_config, ephemeral_config.cmd_env)) {
		ephemeral_config.commands.clear();
		msg_count++;
	}
	dbg2("%zu messages processed", msg_count);
}

static int event_handler(struct event *ev)
{
	static int64_t last_time = 0;
	static int timeout = 0;
	struct key_event kev = {};
	auto vkbd = ::vkbd.get();

	timeout -= ev->timestamp - last_time;
	last_time = ev->timestamp;

	timeout = timeout < 0 ? 0 : timeout;

	switch (ev->type) {
	case EV_TIMEOUT:
		if (!active_kbd)
			return 0;

		kev.code = 0;
		kev.timestamp = ev->timestamp;

		timeout = kbd_process_events(active_kbd, &kev, 1);
		break;
	case EV_DEV_EVENT:
		if (ev->dev->data) {
			struct keyboard *kbd = (struct keyboard*)ev->dev->data;
			active_kbd = kbd;
			switch (ev->devev->type) {
			size_t i;
			case DEV_KEY:
				dbg("input %s %s", KEY_NAME(ev->devev->code), ev->devev->pressed ? "down" : "up");

				kev.code = ev->devev->code;
				kev.pressed = ev->devev->pressed;
				kev.timestamp = ev->timestamp;

				timeout = kbd_process_events(kbd, &kev, 1, true);
				break;
			case DEV_MOUSE_MOVE:
				if (kbd->scroll.active) {
					if (kbd->scroll.sensitivity == 0)
						break;
					int xticks, yticks;

					kbd->scroll.y += ev->devev->y;
					kbd->scroll.x += ev->devev->x;

					yticks = kbd->scroll.y / kbd->scroll.sensitivity;
					kbd->scroll.y %= kbd->scroll.sensitivity;

					xticks = kbd->scroll.x / kbd->scroll.sensitivity;
					kbd->scroll.x %= kbd->scroll.sensitivity;

					vkbd_mouse_scroll(vkbd, 0, -1*yticks);
					vkbd_mouse_scroll(vkbd, 0, xticks);
				} else {
					vkbd_mouse_move(vkbd, ev->devev->x, ev->devev->y);
				}
				break;
			case DEV_MOUSE_MOVE_ABS:
				vkbd_mouse_move_abs(vkbd, ev->devev->x, ev->devev->y);
				break;
			case DEV_LED:
				if (ev->devev->code <= LED_MAX) {
					ev->dev->led_state[ev->devev->code] = ev->devev->pressed;
					// Restore layer indicator state
					if (ev->devev->code == kbd->config.layer_indicator)
						activate_leds(kbd);
				}
				break;
			default:
				break;
			case DEV_MOUSE_SCROLL:
				while (active_kbd && (ev->devev->x || ev->devev->y)) {
					kev.pressed = 1;
					kev.timestamp = ev->timestamp;

					if (ev->devev->x > 0)
						kev.code = KEYD_WHEELLEFT, ev->devev->x--;
					else if (ev->devev->x < 0)
						kev.code = KEYD_WHEELRIGHT, ev->devev->x++;
					else if (ev->devev->y > 0)
						kev.code = KEYD_WHEELUP, ev->devev->y--;
					else if (ev->devev->y < 0)
						kev.code = KEYD_WHEELDOWN, ev->devev->y++;

					kbd_process_events(kbd, &kev, 1);

					kev.pressed = 0;
					// TODO: is it OK to just overwrite timeout?
					timeout = kbd_process_events(kbd, &kev, 1);
				}
				break;
			}
		} else if (ev->dev->is_virtual && ev->devev->type == DEV_LED) {
			/*
			 * Propagate LED events received by the virtual device from userspace
			 * to all grabbed devices.
			 */
			for (auto& dev : device_table) {
				if (dev.fd <= 0)
					break;
				if (dev.data && (dev.capabilities & CAP_LEDS)) {
					struct keyboard* kbd = (struct keyboard*)dev.data;
					if (ev->devev->code <= LED_MAX) {
						// Save LED state for restoring it later
						auto prev = std::exchange(dev.led_state[ev->devev->code], ev->devev->pressed);
						if (prev == ev->devev->pressed)
							continue;
					}
					if (ev->devev->code == kbd->config.layer_indicator) {
						// Suppress indicator change
						continue;
					}
					device_set_led(&dev, ev->devev->code, ev->devev->pressed);
				}

			}
			break;
		}

		break;
	case EV_DEV_ADD:
		manage_device(ev->dev);
		break;
	case EV_DEV_REMOVE:
		keyd_log("DEVICE: r{removed}\t%s %s\n", ev->dev->id, ev->dev->name);

		break;
	case EV_FD_ACTIVITY:
		if (ev->fd == ipcfd) {
			handle_client(accept(ipcfd, NULL, 0));
		}
		break;
	default:
		break;
	}

	vkbd_flush(vkbd);
	return timeout;
}

#ifndef VERSION
#define VERSION "unknown"
#endif

int run_daemon(int, char *[])
{
	ipcfd = ipc_create_server();
	if (ipcfd < 0)
		die("failed to create socket (another instance already running?)");

	vkbd = vkbd_init(VKBD_NAME);

	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	if (nice(-20) == -1) {
		perror("nice");
		exit(-1);
	}

	evloop_add_fd(ipcfd);

	reload(nullptr);

	atexit(cleanup);

	keyd_log("Starting keyd++ " VERSION "\n");
	evloop(event_handler);

	return 0;
}
