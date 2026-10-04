#pragma once

#include <QString>

#include "popupconfig.h"
#include "stylusbuttons.h"

/**
 * Everything stylus-popup reads from its INI config, in one place.
 *
 * The program has one config file - `~/.config/stylus-popup/config.ini`, or
 * $STYLUS_POPUP_CONFIG when set - and exactly one reader: this struct. Each
 * section keeps its own struct next to the code that uses it (`[buttons]` with
 * the button mapping, `[popup]` with the popup), while the file itself is
 * opened once, here. Adding a setting means extending the struct it belongs to
 * and reading it in `load()`.
 *
 * The file is created with the shipped defaults when it is missing; that write
 * is the only one the program ever does, so a config stays hand-editable.
 */
struct AppConfig {
    ButtonMapConfig buttons;
    PopupConfig     popup;

    /** The file the values above came from; never empty after `load()`. */
    QString sourcePath;

    /** Reads the whole file, creating it with the shipped defaults if missing. */
    static AppConfig load(const QString &path = QString());
};
