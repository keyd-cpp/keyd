/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */

#include "keyd.h"
#include <link.h>
#include <charconv>
#include <numeric>

#if (defined(__GLIBCXX__) || defined(__GLIBCPP__)) && !defined(DISABLE_HACKS)

#if defined(__GLIBC__)
extern "C" void _init();

static void call_init(int argc, char **argv, char **envp)
{
	uintptr_t reloc = 0;
	decltype(&call_init) init = nullptr;
	decltype(&call_init)* inits = nullptr;
	size_t init_sz = 0;
	for (size_t i = 0;; i++) {
		auto& dyn = _DYNAMIC[i];
		if (dyn.d_tag == DT_NULL)
			break;
		if (dyn.d_tag == DT_INIT) {
			// Use _init symbol to detect self location in memory
			reloc = uintptr_t(&_init) - dyn.d_un.d_ptr;
			init = decltype(init)(dyn.d_un.d_ptr + reloc);
		}
		if (dyn.d_tag == DT_INIT_ARRAYSZ) {
			init_sz = dyn.d_un.d_val / sizeof(&call_init);
		}
	}

	if (!reloc) {
		fprintf(stderr, "Failed to locate self, exiting.\n");
		exit(-1);
	}

	for (size_t i = 0;; i++) {
		auto& dyn = _DYNAMIC[i];
		if (dyn.d_tag == DT_NULL)
			break;
		if (dyn.d_tag == DT_INIT_ARRAY) {
			inits = decltype(inits)(dyn.d_un.d_ptr + reloc);
		}
	}

	// Call initialization functions
	if (init)
		init(argc, argv, envp);
	for (size_t i = 0; i < init_sz; i++)
		inits[i](argc, argv, envp);
}

// Hijack libc start function to reduce GLIBC requirement
// It seems only powerpc has a different implementation
extern "C" int __libc_start_main(int (*main)(int, char **, char **), int argc, char **argv,
	void (*init)(void),
	void (*fini)(void),
	void (*rtld_fini)(void),
	void *stack_end)
{
	// Some debug stuff ignored (please check twice with glibc sources)
	(void)init; // Should be null
	(void)fini; // Should be null
	if (rtld_fini)
		atexit(rtld_fini);
	(void)stack_end;
	call_init(argc, argv, __environ);
	exit(main(argc, argv, __environ));
}
#endif /* __GLIBC__ */

extern "C" {
	// I don't know how it will behave if we use threads. Just set it to 0 in that case maybe.
	__attribute__((weak)) char __libc_single_threaded = 1;
}

// Seems unused but bumps GLIBC requirement
extern "C" int64_t __pthread_key_create(int64_t)
{
	fprintf(stderr, "%s unimplemented (unexpected)\n", __func__);
	exit(-1);
}

extern "C" int pthread_once(pthread_once_t* once_control, void (*init_routine)(void))
{
	if (!__libc_single_threaded) {
		fprintf(stderr, "%s is single-thread\n", __func__);
		exit(-1);
	}
	if (*once_control == PTHREAD_ONCE_INIT) {
		*once_control = PTHREAD_ONCE_INIT + 1;
		init_routine();
	}
	return 0;
}

template <typename T, typename UT = std::make_unsigned_t<T>>
T isoc23_strto(const char*__restrict__ nptr, char**__restrict__ endptr, int base)
{
	// Rough implementation replacing these dependencies on newer stdlibc++
	// This isoc23 stuff causes lots of problems even on modern distros, but why?
	bool neg = false;
	const char* dummy = nullptr;
	const char** end = endptr ? const_cast<const char**>(endptr) : &dummy;
	if (base) {
		if (base <= 1 || base > 36) {
			*end = nptr;
			return 0;
		}
	}
	while (isspace(nptr[0]))
		nptr++;
	if (!nptr[0]) {
		*end = nptr;
		return 0;
	}
	if (nptr[0] == '+')
		nptr++;
	else if (nptr[0] == '-')
		nptr++, neg = true;
	if (!nptr[0]) {
		*end = nptr;
		return 0;
	}
	std::string_view str(nptr, 2);
	if (str == "0x" || str == "0X") {
		if (base == 0 || base == 16) {
			base = 16;
			nptr += 2;
		} else {
			*end = nptr + 1;
			return 0;
		}
	} else if (str == "0b" || str == "0B") {
		if (base == 0 || base == 2) {
			base = 2;
			nptr += 2;
		} else {
			*end = nptr + 1;
			return 0;
		}
	} else if (str.starts_with("0") && base == 0) {
		// Leading zeros aren't allowed in other cases?
		base = 8;
	} else if (base == 0) {
		base = 10;
	}
	constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyz";
	auto ptr2 = nptr;
	for (size_t i = 0;; i++) {
		if (nptr[i] == '+' || nptr[i] == '-') {
			*end = nptr;
			return 0;
		}
		if (base > 10 && nptr[i] >= 'A' && nptr[i] <= 'Z') {
			if (digits.find_first_of(nptr[i] + 32) >= size_t(base))
				break;
		} else {
			if (digits.find_first_of(nptr[i]) >= size_t(base))
				break;
		}
		if (!nptr[i])
			break;
		ptr2++;
	}

	UT val = 0;
	auto res = std::from_chars(nptr, ptr2, val, base);
	*end = res.ptr;
	if constexpr (std::is_signed_v<T>) {
		if (val > std::numeric_limits<T>::max() + UT(neg))
			res.ec = std::errc::result_out_of_range;
	}
	if (res.ec == std::errc::result_out_of_range) {
		errno = ERANGE;
		if constexpr (std::is_signed_v<T>) {
			if (neg)
				return std::numeric_limits<T>::min();
		}
		return std::numeric_limits<T>::max();
	}
	if (neg)
		return UT(0) - val;
	return val;
}

extern "C" long __isoc23_strtol(const char *__restrict nptr, char **__restrict endptr, int base)
{
	return isoc23_strto<long>(nptr, endptr, base);
}

extern "C" unsigned long __isoc23_strtoul(const char *__restrict nptr, char **__restrict endptr, int base)
{
	return isoc23_strto<unsigned long>(nptr, endptr, base);
}

extern "C" long long __isoc23_strtoll(const char *__restrict nptr, char **__restrict endptr, int base)
{
	return isoc23_strto<long long>(nptr, endptr, base);
}

extern "C" unsigned long long __isoc23_strtoull(const char *__restrict nptr, char **__restrict endptr, int base)
{
	return isoc23_strto<unsigned long long>(nptr, endptr, base);
}

#endif /* libstdc++ hacks */

namespace __cxxabiv1 {
	// Probably makes __cxa_demangle unnecessary
	extern const auto __terminate_handler = abort;
}

// Emulate verbose terminate caller but avoid libunwind (libgcc_eh.a replaced with dummy)
// Removing libunwind also reduces GLIBC requirements
extern "C" void __wrap___cxa_throw(void* ex, std::type_info* tinfo, void (*)(void*))
{
	// Assume only std::exception descendants are thrown
	fprintf(stderr, "Thrown exception of type %s\n\twhat(): %s\n", tinfo->name(), static_cast<std::exception*>(ex)->what());
	abort();
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
