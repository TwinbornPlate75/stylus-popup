# stylus-popup

A Material Design 3 stylus popup notification for the Xiaomi Pad 5, 
displaying battery level, charging status, and attachment state on 
Wayland. It needs modified IDTP9418 driver to receive stylus status.

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
