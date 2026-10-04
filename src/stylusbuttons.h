#pragma once

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <vector>

#include "stylusgestures.h"

/**
 * The pen's two side buttons. They are named after their role on the pen and
 * never after the keyboard key they happen to send: the main (primary) button
 * reports PAGE_DOWN and the secondary one PAGE_UP, the other way round from
 * what those key names suggest.
 */
enum class StylusButton {
    Primary,
    Secondary
};

/** Stable identifier of a button, also exported to the command as STYLUS_BUTTON. */
QString stylusButtonName(StylusButton button);

/**
 * Pen-button -> shell command mapping: the `[buttons]` section of stylus-popup's
 * config. Each button carries one command per gesture; an empty command
 * disables that gesture. Filled in by `AppConfig::load()`, which also writes
 * these values out as the shipped configuration when the file is missing.
 */
struct ButtonMapConfig {
    bool enabled = true;
    bool grab    = true;
    bool repeat  = false;

    /** Gesture thresholds, also user-visible as `double-click-ms`/`long-press-ms`. */
    int doubleClickMs = 300;
    int longPressMs   = 500;

    QString primaryCommand   = QStringLiteral("niri msg action focus-workspace-down");
    QString primaryDouble    = QStringLiteral("niri msg action move-column-to-workspace-down");
    QString primaryLong      = QStringLiteral("niri msg action close-window");
    QString secondaryCommand = QStringLiteral("niri msg action focus-workspace-up");
    QString secondaryDouble  = QStringLiteral("niri msg action move-column-to-workspace-up");
    QString secondaryLong    = QStringLiteral("niri msg action close-window");

    /** Shell command bound to that gesture of that button; empty when unbound. */
    QString commandFor(StylusButton button, StylusGesture gesture) const;
};

/**
 * Watches the Bluetooth HID input nodes the stylus exposes and turns the key
 * events of its two side buttons into gestures: a click, a double click, or a
 * long press, as configured.
 *
 * A node is the pen when its evdev name is one of the two names the supported
 * Xiaomi pens publish *and* it carries PAGE_UP or PAGE_DOWN. The capability
 * half keeps the pen's second, absolute-position-only node out; the exact name
 * keeps the i2c digitizer and ordinary keyboards out.
 *
 * The nodes are exclusive-grabbed (`EVIOCGRAB`) so the key stroke is swallowed
 * instead of reaching the focused application, and `poll()`ed so an absent pen
 * costs nothing. The poll timeout doubles as the gesture timer: it is shortened
 * to the next gesture deadline, which is the only reason an idle monitor ever
 * wakes up before the periodic re-scan. Nodes appearing or disappearing - which
 * is how the pen connects and disconnects - are picked up via inotify plus a
 * slow re-scan.
 */
class StylusButtonMonitor : public QThread
{
    Q_OBJECT

public:
    explicit StylusButtonMonitor(ButtonMapConfig config, QObject *parent = nullptr);
    ~StylusButtonMonitor() override;

    void stop();

    /** Prints the input nodes this monitor would grab - `--list-input`. */
    static void printMatchingDevices(const ButtonMapConfig &config,
                                     const QString &configPath);

signals:
    void gestureTriggered(StylusButton button, StylusGesture gesture, const QString &deviceName);

protected:
    void run() override;

private:
    /** One /dev/input/eventN node, and what we know about it. */
    struct Node {
        QString path;
        QString name;
        QString uniq;  // shown to the user, never matched on
        int     fd           = -1;
        int     openError    = 0;
        bool    grabbed      = false;
        bool    nameMatch    = false;
        bool    hasPrimary   = false;
        bool    hasSecondary = false;

        /** Gesture state, kept per node so a reconnected pen starts clean. */
        ButtonGestureDetector primaryDetector;
        ButtonGestureDetector secondaryDetector;

        /** A node is ours when it is the pen *and* carries a mapped key. */
        bool matches() const { return nameMatch && (hasPrimary || hasSecondary); }
    };

    /** Opens `path` and fills `node`; false only when it cannot be opened.
     *  On success the descriptor stays open for the caller to grab/close. */
    static bool inspect(const QString &path, Node *node);

    static ButtonGestureDetector       &detectorFor(Node &node, StylusButton button);
    static const ButtonGestureDetector &detectorFor(const Node &node, StylusButton button);

    void scan(std::vector<Node> &nodes);
    void attach(std::vector<Node> &nodes, const Node &node);
    bool drain(Node &node, bool dispatch);
    void closeNode(Node &node);

    /** Feeds one raw key event (1 press / 0 release / 2 repeat) to the detector
     *  of `button` and emits the gesture it produces, if any. */
    void feed(Node &node, StylusButton button, int value, int64_t nowMs);

    /** Millisecond value for poll(): the next gesture deadline, else the re-scan. */
    int nextTimeoutMs(const std::vector<Node> &nodes) const;

    /** Emits the gestures whose deadlines have passed. */
    void flushDueGestures(std::vector<Node> &nodes);

    static int64_t monotonicMs();

    static constexpr int kRescanMs = 2000;

    ButtonMapConfig m_config;

    std::atomic<bool> m_running{true};
    int               m_wakeFd{-1};
    bool              m_openWarningLogged{false};
};

/**
 * Glue between the monitor and the configured shell commands: runs the command
 * bound to a button gesture through `/bin/sh -c` and exports `STYLUS_BUTTON`
 * plus `STYLUS_GESTURE`, so a single command can serve every button and gesture.
 * The mapping is handed in by `main()`, which reads the config file once.
 */
class StylusButtonMapper : public QObject
{
    Q_OBJECT

public:
    explicit StylusButtonMapper(const ButtonMapConfig &config, QObject *parent = nullptr);
    ~StylusButtonMapper() override;

    void start();

private slots:
    void onGestureTriggered(StylusButton button, StylusGesture gesture,
                            const QString &deviceName);

private:
    ButtonMapConfig      m_config;
    StylusButtonMonitor *m_monitor;
};
