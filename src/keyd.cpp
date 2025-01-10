/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include "keyd.h"

extern "C" {
	__attribute__((weak)) char __libc_single_threaded = 1;
}

extern "C" char* __cxa_demangle(const char* mangled_name, char* output_buffer, size_t* length, int* status)
{
	// Return mangled name to remove demangler from ELF
	std::string_view name = mangled_name;
	void* buf = nullptr;
	if (length) {
		if (output_buffer && *length >= name.size() + 1)
			buf = output_buffer;
		*length = name.size() + 1;
	}
	if (!buf) {
		buf = output_buffer ? realloc(output_buffer, name.size() + 1) : malloc(name.size() + 1);
	}
	if (status)
		*status = 0;
	memcpy(buf, mangled_name, name.size() + 1);
	return static_cast<char*>(buf);
}

extern "C" int64_t __pthread_key_create(int64_t)
{
	return 0;
}

extern "C" int pthread_once (pthread_once_t *__once_control, void (*__init_routine) (void))
{
	if (!*__once_control) {
		*__once_control = 1;
		__init_routine();
	}
	return 0;
}

static int ipc_exec(enum ipc_msg_type_e type, const char *data, size_t sz, uint32_t timeout)
{
	struct ipc_message msg;

	assert(sz <= sizeof(msg.data));

	msg.type = type;
	msg.sz = sz;
	msg.timeout = timeout;
	memcpy(msg.data, data, sz);

	static int con = -1;
	if (con == -1) {
		con = ipc_connect();
		if (con < 0) {
			perror("connect");
			exit(-1);
		}
	}

	xwrite(con, &msg, sizeof msg);
	if (!xread(con, &msg, sizeof msg))
		exit(-1);

	if (msg.sz) {
		xwrite(1, msg.data, msg.sz);
		xwrite(1, "\n", 1);
	}

	return msg.type == IPC_FAIL;
}

#ifndef VERSION
#define VERSION "unknown"
#endif

static int version(int, char *[])
{
	printf("keyd++ " VERSION "\n");

	return 0;
}

static int help(int, char *[])
{
	printf("usage: keyd [-v] [-h] [command] [<args>]\n\n"
	       "Commands:\n"
	       "    monitor [-t]                   Print key events in real time.\n"
	       "    list-keys                      Print a list of valid key names.\n"
	       "    reload                         Trigger a reload .\n"
	       "    listen                         Print layer state changes of the running keyd++ daemon to stdout.\n"
	       "    bind <binding> [<binding>...]  Add the supplied bindings to all loaded configs.\n"
	       "Options:\n"
	       "    -v, --version      Print the current version and exit.\n"
	       "    -h, --help         Print help and exit.\n");

	return 0;
}

static int list_keys(int, char *[])
{
	for (size_t i = 0; i < KEY_CNT; i++) {
		const char *altname = keycode_table[i].alt_name;
		const char *shiftedname = keycode_table[i].shifted_name;
		const char *name = keycode_table[i].name().data();

		printf("key_%03zu: ", i);
		if (name)
			printf("'%s'", name);
		if (altname)
			printf(" or '%s'", altname);
		if (shiftedname)
			printf(" (shifted '%s')", shiftedname);
		printf("\n");
	}

	for (int i = KEY_CNT; i < KEYD_ENTRY_COUNT; i++) {
		const char *altname = keycode_table[i].alt_name;
		const char *name = keycode_table[i].b_name;

		if (name) {
			printf("special: '%s'", name);
			if (altname)
				printf(" or '%s'", altname);
			printf(" (key_%d)\n", i);
		}
	}

	return 0;
}


static int add_bindings(int argc, char *argv[])
{
	int i;
	int ret = 0;

	for (i = 1; i < argc; i++) {
		if (ipc_exec(IPC_BIND, argv[i], strlen(argv[i]), 0))
			ret = -1;
	}

	if (!ret)
		printf("Success\n");

	return ret;
}

static void read_input(int argc, char *argv[], char *buf, size_t *psz)
{
	size_t sz = 0;
	size_t bufsz = *psz;

	if (argc != 0) {
		int i;
		for (i = 0; i < argc; i++) {
			sz += snprintf(buf+sz, bufsz-sz, "%s%s", argv[i], i == argc-1 ? "" : " ");

			if (sz >= bufsz)
				die("maximum input length exceeded");
		}
	} else {
		while (1) {
			size_t n;

			if ((n = read(0, buf+sz, bufsz-sz)) <= 0)
				break;
			sz += n;

			if (bufsz == sz)
				die("maximum input length exceeded");
		}
	}

	*psz = sz;
}

static int cmd_do(int argc, char *argv[])
{
	char buf[MAX_IPC_MESSAGE_SIZE];
	size_t sz = sizeof buf;
	uint32_t timeout = 0;

	if (argc > 2 && !strcmp(argv[1], "-t")) {
		timeout = atoi(argv[2]);
		argc -= 2;
		argv += 2;
	}

	read_input(argc-1, argv+1, buf, &sz);

	return ipc_exec(IPC_MACRO, buf, sz, timeout);
}


static int input(int argc, char *argv[])
{
	char buf[MAX_IPC_MESSAGE_SIZE];
	size_t sz = sizeof buf;
	uint32_t timeout = 0;

	if (argc > 2 && !strcmp(argv[1], "-t")) {
		timeout = atoi(argv[2]);
		argc -= 2;
		argv += 2;
	}

	read_input(argc-1, argv+1, buf, &sz);

	return ipc_exec(IPC_INPUT, buf, sz, timeout);
}

static int layer_listen(int, char *[])
{
	struct ipc_message msg = {};

	int con = ipc_connect();

	if (con < 0) {
		perror("connect");
		exit(-1);
	}

	msg.type = IPC_LAYER_LISTEN;
	xwrite(con, &msg, sizeof msg);

	while (1) {
		char buf[512];
		ssize_t sz;

		struct pollfd pfds[] = {
			{1, POLLERR, 0},
			{con, POLLIN, 0},
		};

		if (poll(pfds, 2, -1) < 0) {
			perror("poll");
			exit(-1);
		}

		if (pfds[0].revents)
			return -1;

		if (pfds[1].revents) {
			sz = read(con, buf, sizeof buf);
			if (sz <= 0)
				return -1;

			xwrite(1, buf, sz);
		}
	}
}

static int reload(int, char *[])
{
	ipc_exec(IPC_RELOAD, NULL, 0, 0);

	return 0;
}

struct {
	const char *name;
	const char *flag;
	const char *long_flag;

	int (*fn)(int argc, char **argv);
} commands[] = {
	{"help", "-h", "--help", help},
	{"version", "-v", "--version", version},

	/* Keep -e and -m for backward compatibility. TODO: remove these at some point. */
	{"monitor", "-m", "--monitor", monitor},
	{"bind", "-e", "--expression", add_bindings},
	{"input", "", "", input},
	{"do", "", "", cmd_do},

	{"listen", "", "", layer_listen},

	{"reload", "", "", reload},
	{"list-keys", "", "", list_keys},
};

int main(int argc, char *argv[])
{
	size_t i;

	log_level =
	    atoi(getenv("KEYD_DEBUG") ? getenv("KEYD_DEBUG") : "");

	if (isatty(1))
		suppress_colours = getenv("NO_COLOR") ? 1 : 0;
	else
		suppress_colours = 1;

	dbg("Debug mode activated");

	signal(SIGTERM, exit);
	signal(SIGINT, exit);
	signal(SIGPIPE, SIG_IGN);

	if (argc > 1) {
		for (i = 0; i < ARRAY_SIZE(commands); i++)
			if (!strcmp(commands[i].name, argv[1]) ||
				!strcmp(commands[i].flag, argv[1]) ||
				!strcmp(commands[i].long_flag, argv[1])) {
				return commands[i].fn(argc - 1, argv + 1);
			}

		return help(argc, argv);
	}

	memcpy(argv[0], "keyd++", 7);
	run_daemon(argc, argv);
}
