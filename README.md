# stylus-popup

A Dynamic Island style status pill for the Xiaomi Pad 5's active stylus, on Wayland.
It reads the modified **IDTP9418** driver (`/dev/idtp9418`), shows the pen's
battery state in a `wlr-layer-shell` overlay at the top of the screen, and pairs
the pen over Bluetooth as soon as it attaches.

It also turns the pen's two side buttons into shell commands, with a click,
a double click and a long press each getting its own command.

## What it shows

| Pen state | Island |
| --- | --- |
| Attached, pairing still in flight | Compact pill: MD3 progress indicator and `Connecting…` |
| Attached, paired and connected | Expanded pill: battery ring, capacity, `Charging` / `Connected` with the charge limit, and a bolt chip while charging |
| Pairing failed or timed out | Compact pill with an error icon; shakes, then goes away |
| Detached | Shrinks back into a dot and disappears |

The island grows out of a dot at the top centre of the screen and morphs
between its forms on spring physics: the old content fades out, the shape
morphs, and the new content fades in once the shape has mostly arrived. Inner
elements are concentric with the rounded outline. A change in charging state
gives the island a short pulse, and the battery ring follows the capacity
smoothly. The expanded island collapses again a few seconds after it appears.

Colours follow the matugen-generated qt6ct scheme
(`~/.config/qt6ct/colors/matugen.conf`) and update live when that file
changes; a built-in palette is used otherwise. Text is Chinese under a Chinese
system locale and English otherwise. The surface has an empty input region,
so it never blocks clicks on the bar beneath it.

## Requirements

- A Wayland compositor with `zwlr_layer_shell_v1`. This is used on **niri**;
  the popup itself is compositor-agnostic, but the default button commands are
  `niri msg` / `wlrctl` specific and can be replaced.
- The modified IDTP9418 driver exposing `/dev/idtp9418`.
- D-Bus and BlueZ, for the automatic pairing.
- Membership of the `input` group, for the button mapping:

  ```sh
  sudo usermod -aG input "$USER"   # then log out and back in
  ```

Build dependencies: `cmake` ≥ 3.16, a C++17 compiler, Qt 6 (`Widgets`, `Gui`,
`DBus`) and `wayland-client` / `wayland-scanner` via `pkg-config`.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Install

```sh
sudo cmake --install build
```

This installs two files: `/usr/bin/stylus-popup`, and a **user** systemd unit at
`/usr/lib/systemd/user/stylus-popup.service`. Enable it as your own user, with
no `sudo`:

```sh
systemctl --user enable --now stylus-popup.service
systemctl --user status  stylus-popup.service
```

It is a user unit because the popup is a Wayland client: it needs the session's
`WAYLAND_DISPLAY`, `NIRI_SOCKET` and `XDG_RUNTIME_DIR`, which the user manager
already provides. It starts and stops with `graphical-session.target`. If a
system-wide `stylus-popup.service` is still enabled from an older install,
disable it first — two instances would fight over the pen's input node:

```sh
sudo systemctl disable --now stylus-popup.service
```

Pass `-DCMAKE_INSTALL_PREFIX=...` to install elsewhere. On Fedora / RPM-based
distributions, `./packaging/build-rpm.sh` builds a binary RPM.

## Stylus buttons

The pen's two side buttons arrive over Bluetooth HID as ordinary keyboard keys.
stylus-popup grabs the matching input node so the key stroke no longer reaches
the focused window, and runs a command instead. The main button is `primary` and
the secondary one is `secondary`.

Each button carries three gestures: **click** (released before the long-press
threshold), **double click** (a second click within `double-click-ms`) and
**long press** (held for `long-press-ms`). A single press produces at most one
gesture.

### Configuration

The mapping lives in `~/.config/stylus-popup/config.ini`, created with these
defaults on first run:

```ini
[buttons]
enabled=true
grab=true
repeat=false
double-click-ms=300
long-press-ms=500

primary=niri msg action focus-workspace-down
primary-double-click=niri msg action move-column-to-workspace-down
primary-long-press=niri msg action close-window
secondary=niri msg action focus-workspace-up
secondary-double-click=niri msg action move-column-to-workspace-up
secondary-long-press=niri msg action close-window

[popup]
connect-timeout-ms=15000
```

| Key | Meaning |
| --- | --- |
| `enabled` | Master switch; `false` leaves the buttons completely alone |
| `grab` | Grab the node so the key stroke is swallowed; `false` still runs the command but lets the key through |
| `repeat` | Also fire on key auto-repeat while a button is held |
| `double-click-ms` | How long a click waits for a second one |
| `long-press-ms` | How long a press must be held to count as a long press |
| `primary`, `secondary` | Command for a click |
| `primary-double-click`, `secondary-double-click` | Command for a double click |
| `primary-long-press`, `secondary-long-press` | Command for a long press |

An empty command disables that gesture. Both thresholds are clamped to
50-10000 ms, and a value that is not a number keeps the default.

`connect-timeout-ms` in `[popup]` is how long the pen has to connect after it
attaches. When the wait runs out the popup slides away and the pending attempt
is dropped, so nothing retries it until the pen is attached again; `0` waits
forever. It is clamped to 0-600000 ms, and a value that is not a number keeps
the default.

A command is handed to `/bin/sh -c` with `STYLUS_BUTTON` (`primary` or
`secondary`) and `STYLUS_GESTURE` (`single`, `double-click`, `long-press`)
exported, so one script can serve every button and gesture:

```sh
case "$STYLUS_BUTTON/$STYLUS_GESTURE" in
    secondary/long-press) wlrctl pointer click right ;;
esac
```

Bind a script rather than inlining a command with more than one statement: the
config reader truncates a value at the first unescaped `;`.

Set `STYLUS_POPUP_CONFIG` to point at a different file.

## Diagnostics

`--list-input` prints the config that is in effect and what would happen to
every input node, without needing a Wayland session. It is the first thing to
check when a button does nothing:

```sh
stylus-popup --list-input
```

A node that cannot be opened reports its `errno` instead of its capabilities,
which is the usual sign of a missing `input` group membership.

The running service logs each decoded gesture along with the command it ran:

```sh
journalctl --user -u stylus-popup -f
```

## License

MIT
