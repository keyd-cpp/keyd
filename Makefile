.PHONY: all clean install deb uninstall debug man compose test-harness
VERSION=3.0.6
COMMIT=$(shell git describe --no-match --always --abbrev=7 --dirty)
VKBD?=uinput
PREFIX?=/usr/local

CONFIG_DIR?=/etc/keyd
SOCKET_PATH=/var/run/keyd.socket

# If this variable is set to the empty string, no systemd unit files will be
# installed.
SYSTEMD_SYSTEM_DIR = /usr/lib/systemd/system

CFLAGS:=-DVERSION=\"v$(VERSION)\ \($(COMMIT)\)\" \
	-I/usr/local/include \
	-L/usr/local/lib \
	-Wall \
	-Wextra \
	-Wno-unused \
	-std=c11 \
	-DSOCKET_PATH=\"$(SOCKET_PATH)\" \
	-DCONFIG_DIR=\"$(CONFIG_DIR)\" \
	-DDATA_DIR=\"$(PREFIX)/share/keyd\" \
	-D_FORTIFY_SOURCE=2 \
	-D_DEFAULT_SOURCE \
	-Werror=format-security \
	$(CFLAGS)

CXXFLAGS:=-DVERSION=\"v$(VERSION)\ \($(COMMIT)\)\" \
	-I/usr/local/include \
	-L. -L/usr/local/lib \
	-Wall \
	-Wextra \
	-Wno-unused \
	-std=c++20 \
	-DSOCKET_PATH=\"$(SOCKET_PATH)\" \
	-DCONFIG_DIR=\"$(CONFIG_DIR)\" \
	-DDATA_DIR=\"$(PREFIX)/share/keyd\" \
	-D_FORTIFY_SOURCE=2 \
	-D_DEFAULT_SOURCE \
	-Werror=format-security \
	-fdata-sections -ffunction-sections \
	-static-libgcc -static-libstdc++\
	$(CXXFLAGS)

platform=$(shell uname -s)

ifeq ($(platform), Linux)
	COMPAT_FILES=
else
	LDFLAGS+=-linotify
	COMPAT_FILES=
endif

all: compose man
	mkdir -p bin
	cp scripts/keyd-application-mapper bin/
	$(CXX) $(CXXFLAGS) -O3 $(COMPAT_FILES) src/*.cpp src/vkbd/$(VKBD).cpp -Wl,--gc-sections -o bin/keyd $(LDFLAGS)
debug:
	CFLAGS="-g -fsanitize=address -Wunused" $(MAKE)
compose:
	mkdir -p data
	./scripts/generate_xcompose
man:
	for f in docs/*.scdoc; do \
		target=$${f%%.scdoc}.1.gz; \
		target=data/$${target##*/}; \
		scdoc < "$$f" | gzip -9 > "$$target"; \
	done
deb:
	-rm -r keyd++-$(VERSION)
	mkdir -p -m775 keyd++-$(VERSION)
	mkdir -p -m775 keyd++-$(VERSION)/bin
	mkdir -p -m775 keyd++-$(VERSION)/DEBIAN
	mkdir -p -m775 keyd++-$(VERSION)/etc/keyd
	mkdir -p -m775 keyd++-$(VERSION)/usr/share/keyd/layouts/
	mkdir -p -m775 keyd++-$(VERSION)/usr/share/man/man1/
	mkdir -p -m775 keyd++-$(VERSION)/usr/share/doc/keyd/examples/
	mkdir -p -m775 keyd++-$(VERSION)/usr/lib/systemd/system

	install -m775 bin/keyd keyd++-$(VERSION)/bin/
	install -m664 data/default.conf keyd++-$(VERSION)/etc/keyd/
	install -m664 docs/*.md keyd++-$(VERSION)/usr/share/doc/keyd/
	cat LICENSE > keyd++-$(VERSION)/usr/share/doc/keyd/copyright
	install -m664 examples/* keyd++-$(VERSION)/usr/share/doc/keyd/examples/
	install -m664 layouts/* keyd++-$(VERSION)/usr/share/keyd/layouts
	install -m664 data/keyd.1.gz keyd++-$(VERSION)/usr/share/man/man1/
	install -m775 scripts/postinst keyd++-$(VERSION)/DEBIAN/
	sed -e 's#@PREFIX@##' keyd.service.in > keyd++-$(VERSION)/usr/lib/systemd/system/keyd.service

	touch keyd++-$(VERSION)/DEBIAN/conffiles
	echo "/etc/keyd/default.conf" >> keyd++-$(VERSION)/DEBIAN/conffiles
	touch keyd++-$(VERSION)/DEBIAN/control
	echo "Package: keyd++" >> keyd++-$(VERSION)/DEBIAN/control
	echo "Version: $(VERSION)" >> keyd++-$(VERSION)/DEBIAN/control
	echo "Priority: optional" >> keyd++-$(VERSION)/DEBIAN/control
	echo "Architecture: "`dpkg-architecture -q DEB_TARGET_ARCH` >> keyd++-$(VERSION)/DEBIAN/control
	echo "Maintainer: Nekotekina <nekotekina@gmail.com>" >> keyd++-$(VERSION)/DEBIAN/control
	echo "Homepage: https://github.com/keyd-cpp/" >> keyd++-$(VERSION)/DEBIAN/control
	echo "Description: Simple system-wide key remapping daemon" >> keyd++-$(VERSION)/DEBIAN/control
	echo "Conflicts: keyd" >> keyd++-$(VERSION)/DEBIAN/control
	echo "Replaces: keyd" >> keyd++-$(VERSION)/DEBIAN/control
	echo "Depends: libc6 (>= 2.17), systemd" >> keyd++-$(VERSION)/DEBIAN/control

	dpkg-deb -Zgzip --root-owner-group --build keyd++-$(VERSION) keyd++-$(VERSION).`dpkg-architecture -q DEB_TARGET_ARCH`.deb
	rm -r keyd++-$(VERSION)

install:

	@if [ -n '$(SYSTEMD_SYSTEM_DIR)' ]; then \
		sed -e 's#@PREFIX@#$(PREFIX)#' keyd.service.in > keyd.service; \
		mkdir -p '$(DESTDIR)$(SYSTEMD_SYSTEM_DIR)'; \
		install -Dm644 keyd.service '$(DESTDIR)$(SYSTEMD_SYSTEM_DIR)/keyd.service'; \
	fi

	@if [ "$(VKBD)" = "usb-gadget" ]; then \
		if [ -n '$(SYSTEMD_SYSTEM_DIR)' ]; then \
			sed -e 's#@PREFIX@#$(PREFIX)#' src/vkbd/usb-gadget.service.in > src/vkbd/usb-gadget.service; \
			install -Dm644 src/vkbd/usb-gadget.service '$(DESTDIR)$(SYSTEMD_SYSTEM_DIR)/keyd-usb-gadget.service'; \
		fi; \
		install -Dm755 src/vkbd/usb-gadget.sh $(DESTDIR)$(PREFIX)/bin/keyd-usb-gadget.sh; \
	fi

	mkdir -p $(DESTDIR)$(CONFIG_DIR)
	mkdir -p $(DESTDIR)$(PREFIX)/bin/
	mkdir -p $(DESTDIR)$(PREFIX)/share/keyd/
	mkdir -p $(DESTDIR)$(PREFIX)/share/keyd/layouts/
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1/
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/keyd/
	mkdir -p $(DESTDIR)$(PREFIX)/share/doc/keyd/examples/

	-groupadd keyd
	install -m755 bin/keyd bin/keyd-application-mapper $(DESTDIR)$(PREFIX)/bin/
	install -m644 docs/*.md $(DESTDIR)$(PREFIX)/share/doc/keyd/
	install -m644 examples/* $(DESTDIR)$(PREFIX)/share/doc/keyd/examples/
	install -m644 layouts/* $(DESTDIR)$(PREFIX)/share/keyd/layouts
	cp -r data/gnome-* $(DESTDIR)$(PREFIX)/share/keyd
	install -m644 data/*.1.gz $(DESTDIR)$(PREFIX)/share/man/man1/
	install -m644 data/keyd.compose $(DESTDIR)$(PREFIX)/share/keyd/

uninstall:
	-groupdel keyd
	[ -z '$(SYSTEMD_SYSTEM_DIR)' ] || rm -f \
		'$(DESTDIR)$(SYSTEMD_SYSTEM_DIR)/keyd.service' \
		'$(DESTDIR)$(SYSTEMD_SYSTEM_DIR)/keyd-usb-gadget.service'
	rm -rf $(DESTDIR)$(PREFIX)/bin/keyd \
		$(DESTDIR)$(PREFIX)/bin/keyd-application-mapper \
		$(DESTDIR)$(PREFIX)/share/doc/keyd/ \
		$(DESTDIR)$(PREFIX)/share/man/man1/keyd*.gz \
		$(DESTDIR)$(PREFIX)/share/keyd/ \
		$(DESTDIR)$(PREFIX)/bin/keyd-usb-gadget.sh
clean:
	rm -rf bin data/*.1.gz data/keyd.compose keyd.service src/unicode.cpp src/vkbd/usb-gadget.service
test:
	@cd t; \
	for f in *.sh; do \
		./$$f; \
	done
test-io:
	mkdir -p bin
	$(CXX) \
	-std=c++20 -g -O2 \
	-DDATA_DIR= \
	-o bin/test-io \
		t/test-io.cpp \
		src/keyboard.cpp \
		src/string.cpp \
		src/macro.cpp \
		src/config.cpp \
		src/log.cpp \
		src/keys.cpp  \
		src/unicode.cpp && \
	./bin/test-io t/test.conf t/*.t
