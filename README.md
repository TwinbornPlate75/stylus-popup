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
