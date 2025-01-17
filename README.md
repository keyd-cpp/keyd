# keyd++

Linux lacks a good key remapping solution. In order to achieve satisfactory
results a medley of tools need to be employed (e.g xcape, xmodmap) with the end
result often being tethered to a specified environment (X11). keyd attempts to
solve this problem by providing a flexible system wide daemon which remaps keys
using kernel level input primitives (evdev, uinput).

keyd++ is a fork of keyd and has some additional features at the moment:

- **Wildcard bindings**: `**numlock = **pause` replaces keyd's `numlock = pause`.
- **Precise bindings**: explicit left-hand modifiers `M-f = x` or absence thereof.
- Rebind **any** key by its number like `key_333`. May improve forward compatibility.
- **Implicit macro** like M-f now tries to behave like a normal key binding (not a tap).
- **Macro** can now do `type(Hello world)` without worrying about spaces.
- Bound **Macro** can now do `cmd(gnome-terminal)` and it should **just work**.
- Wildcard for mice `m:` that excludes problematic abs ptr devices(`a:`).
- Layer indicator with keyboard led of choice (keyd has been somewhat broken).
- New 3 unused modifiers and the ability to override modifier keys.
- Memory optimizations (you only spend more memory if you config is bigger).
- Performance optimizations, eg. events from ungrabbed device are ignored.
- Some security improvement: privileged commands only run from /etc/keyd/ conf.
- Bindings coming from ex. `keyd-application-mapper` inherit user credentials.
- `keyd reload` from user loads user binds from `~/.config/keyd/bindings.conf`.
- Convenience and safety coming from C++20. Sorry for longer compilation.
- Physical keys variants (left or right) are pressed accordingly.
- Virtually unlimited sizes/counts (keyd has had many hardcoded limitations).
- Partially compatible with keyd 2.5.0 configs, compatibility mode exists.

# Goals

  - Fast
  - Simple
  - Obvious
  - Powerful

# Features

- Layers. Layer is a set of key bindings. Layers do combine.
- Key overloading (different behaviour on tap/hold).
- Keyboard specific configuration.
- Instantaneous remapping (no more flashing :)).
- A client-server model that facilitates scripting and display server agnostic application remapping. (Currently ships with support for X, sway, and gnome (wayland)).
- System wide config (works in a VT).
- First class support for modifier overloading.
- Unicode support.

### keyd is for people who:

 - Would like to experiment with custom [layers](https://docs.qmk.fm/feature_layers) (i.e custom shift keys)
   and oneshot modifiers.
 - Want to have multiple keyboards with different layouts on the same machine.
 - Want to be able to remap `C-1` without breaking modifier semantics.
 - Want a keyboard config format which is easy to grok.
 - Like tiny daemons that adhere to the Unix philosophy.
 - Want to put the control and escape keys where God intended.
 - Wish to be able to switch to a VT to debug something without breaking their keymap.

# Dependencies

 - C++20 compiler starting from clang++-14 or g++-11 (can use cross-compiler)
 - Linux kernel headers (already present on most systems)
 - Runtime dependency glibc>=2.17 but can be built against Musl C library

## Optional

 - python      (for application specific remapping)
 - python-xlib (only for X support)
 - dbus-python (only for KDE support)

# Installation

*Note:* master serves as the development branch, things may occasionally break.

## From Source

```bash
# Install dependencies (if necessary)
sudo apt install build-essentials git
# Clone with git clone or download sources manually to keyd directory
git clone https://github.com/keyd-cpp/keyd.git
cd keyd
# Specify your favourite compiler and options (optional)
export CXX=clang++-18 CXXFLAGS=-s
# First time install
make && sudo make install && sudo systemctl enable --now keyd
# Second time install (update example)
make && sudo make install && sudo systemctl daemon-reload && sudo systemctl restart keyd
```

# Quickstart

1. Install and start keyd (e.g `sudo systemctl enable keyd --now`)

2. Put the following in `/etc/keyd/default.conf`:

```
[ids]

# Capture all keyboards
k:*

[main]

# Remaps numlock with any modifier to pause/break keeping modifiers
**numlock = **pause
# Access numlock itself via Super+Numlock
M-numlock = numlock

# Maps capslock to escape when pressed and control when held.
capslock = overload(control, esc)
# When capslock is pressed with other modifiers, disable esc.
**capslock = layer(control)

# Remaps the escape key (without modifiers) to capslock
esc = capslock
```

Key names can be obtained by using the `keyd monitor` command. Note that while keyd is running, the output of this
command will correspond to keyd's output. The original input events can be seen by first stopping keyd and then
running the command. See the man page for more details.

3. Run `sudo keyd reload` to reload the config set.

4. See the man page (`man keyd`) for a more comprehensive description.

Config errors will appear in the log output and can be accessed in the usual
way using your system's service manager (e.g `sudo journalctl -eu keyd`).

*Note*: It is possible to render your machine unusable with a bad config file.
Should you find yourself in this position, the special key sequence
`backspace+escape+enter` should cause keyd to terminate.

Some mice (e.g the Logitech MX Master) are capable of emitting keys and
are consequently matched by the wildcard id. It may be necessary to
explicitly blacklist these.

## User-Level Remapping (experimental)

- Add yourself to the keyd group:

	`usermod -aG keyd <user>`

- Create `~/.config/keyd/bindings.conf`:

	E.G. `M-t = macro(cmd(gnome-terminal))`

- Execute `keyd reload` without sudo (possibly at startup).
This is important as mapped commands will run with user privileges.

## Application Specific Remapping (experimental)

- Add yourself to the keyd group:

	`usermod -aG keyd <user>`

- Populate `~/.config/keyd/app.conf`:

E.G

	[alacritty]

	A-] = macro(C-g n)
	A-[ = macro(C-g p)

	[chromium]

	A-[ = C-S-tab
	A-] = macro(C-tab)

	[org-gnome-nautilus]
	# This Meta+F key should open selected file in Nautilus with Firefox.
	M-f = macro(C-c 50ms cmd(xsel -ocb | xargs -0 firefox))

- Run:

	`keyd-application-mapper`

You will probably want to put `keyd-application-mapper -d` somewhere in your
display server initialization logic (e.g ~/.xinitrc) unless you are running Gnome.

See the man page for more details.

## SBC support

Experimental support for single board computers (SBCs) via usb-gadget
has been added courtesy of Giorgi Chavchanidze.

See [usb-gadget.md](src/vkbd/usb-gadget.md) for details.

## Packages

Third party packages for the some distributions.

# Example config

Many users will probably not be interested in taking full advantage of keyd.
For those who seek simple quality of life improvements I can recommend the
following config:

	[ids]

	k:*

	[main]

	**shift = oneshot(shift)
	**meta = oneshot(meta)
	**control = oneshot(control)
	**alt = oneshot(alt)
	**altgr = oneshot(altgr)

	capslock = overload(control, esc)
	**capslock = layer(control)
	insert = S-insert

This overloads the capslock key to function as both escape (when tapped) and
control (when held) and remaps all modifiers to 'oneshot' keys. Thus to produce
the letter A you can now simply tap shift and then a instead of having to hold
it. Finally it remaps insert to S-insert (paste on X11).
