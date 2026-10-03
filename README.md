# stylus-popup

A Material Design 3 stylus popup notification for the Xiaomi Pad 5,
running on Wayland. It listens to a modified IDTP9418 kernel driver for
stylus status and shows a floating capsule with battery level, charging
state, and generation-specific model name.

## Features

- Floating capsule popup anchored to the top of the screen
- Circular battery glyph with charging glow and low-battery color
- "Connecting…" spinner while the stylus is attaching
- Automatic Bluetooth pairing via BlueZ once the pen attaches
- Charge limit badge (`LIMIT %`)
- Auto-detected stylus generation: shown as **Xiaomi Stylus Pen 2** when
the MAC address matches `E6:FB:D0:E1:5A:04`, otherwise **Xiaomi Stylus Pen 1**
- The pen's two side buttons are intercepted and mapped to shell commands
(see [Stylus buttons](#stylus-buttons))

## Build

Requirements:

- Qt 5 or Qt 6 (`Widgets`, `Gui`, `DBus`)
- `wayland-client`
- `wayland-scanner`
- `cmake` ≥ 3.16
- A C++17 compiler

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The generated `compile_commands.json` ends up in the build directory; export
it with `cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` if your editor
needs it.

## Install

```sh
sudo cmake --install build
```

A user-level systemd unit is provided in `packaging/stylus-popup.service`;
install it to `~/.config/systemd/user/` and `systemctl --user enable
--now stylus-popup.service` to start at login.

### Fedora / RPM-based distributions

Run the bundled script to install build dependencies, build the source
tarball from the current commit, and produce a binary RPM in one step:

```sh
./packaging/build-rpm.sh
```

Use `./packaging/build-rpm.sh --srpm` to also
emit a source RPM, or `./packaging/build-rpm.sh clean` to wipe the
`~/rpmbuild` tree and `build/` between runs.

## Runtime requirements

- Wayland compositor
- Modified IDTP9418 driver exposing `/dev/idtp9418`
- D-Bus system bus and BlueZ for automatic stylus pairing
- Read access to `/dev/input/event*`, i.e. membership of the `input` group -
  required by the stylus button mapping:

  ```sh
  sudo usermod -aG input "$USER"   # then log out and back in
  ```

## Stylus buttons

The pen's two side buttons arrive over Bluetooth HID as ordinary keyboard keys
(`PAGE_UP` and `PAGE_DOWN`). stylus-popup grabs the matching input node so the
key stroke no longer reaches the focused window, and runs a shell command
instead.

The mapping lives in `~/.config/stylus-popup/config.ini`, which is created with
these defaults on first run:

```ini
[buttons]
enabled=true
grab=true
repeat=false

page-up=niri msg action focus-workspace-up
page-down=niri msg action focus-workspace-down
```

| Key | Meaning |
| --- | --- |
| `enabled` | Master switch; `false` leaves the buttons untouched |
| `grab` | Grab the node exclusively, swallowing the key stroke. `false` runs the command but lets the key through as well |
| `repeat` | Also fire the command on key auto-repeat while a button is held |
| `page-up`, `page-down` | Shell command run for that button; empty disables it |

Commands are passed to `/bin/sh -c`, with `STYLUS_BUTTON` exported as `page-up`
or `page-down`, so one command can serve both buttons:

```ini
page-up=/home/me/bin/pen.sh
page-down=/home/me/bin/pen.sh
```

```sh
# ~/bin/pen.sh
case "$STYLUS_BUTTON" in
    page-up)   niri msg action focus-workspace-up ;;
    page-down) niri msg action focus-workspace-down ;;
esac
```

The pen is recognised by its evdev device name, which is hardcoded: stylus-popup
only supports the Xiaomi Stylus Pen 1st and 2nd gen, and both advertise the same
Bluetooth name `Xiaomi Smart Pen`. The kernel derives two node names from it -
`Xiaomi Smart Pen Keyboard`, which carries the side buttons, and
`Xiaomi Smart Pen`, the pen's absolute-position-only node. A node is grabbed
when its name is one of those *and* it exposes `PAGE_UP` or `PAGE_DOWN`: the
capability half keeps the pen's second node out, and the exact name keeps the
i2c digitizer (`NVTCapacitivePen`) and ordinary keyboards out. To see which
nodes are candidates, which one would be grabbed, and whether the `input`
group is in the way:

```sh
stylus-popup --list-input   # prints every input node and what would be grabbed
```

Set `STYLUS_POPUP_CONFIG` to point at a different config file, which is handy
for trying a mapping out before making it permanent.

## Layout

```
.
├── CMakeLists.txt
├── README.md
├── src/                C++ sources (Qt + Wayland)
├── protocols/          Wayland protocol XML
└── packaging/          RPM spec and systemd unit
```

## License

MIT
