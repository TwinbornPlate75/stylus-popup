#pragma once

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <vector>

/** The two side buttons of the pen, as reported over Bluetooth HID. */
enum class StylusButton {
    PageUp,
    PageDown
};

/** Stable identifier of a button, also exported to the command as STYLUS_BUTTON. */
QString stylusButtonName(StylusButton button);

/**
 * Pen-button -> shell command mapping, read from the `[buttons]` section of
 * stylus-popup's INI config. The file is created with these values on first
 * run, so the defaults below double as the shipped configuration.
 */
struct ButtonMapConfig {
    bool    enabled = true;
    bool    grab    = true;
    bool    repeat  = false;
    QString pageUpCommand   = QStringLiteral("niri msg action focus-workspace-up");
    QString pageDownCommand = QStringLiteral("niri msg action focus-workspace-down");

    /** Where the settings were read from (empty when nothing was loaded). */
    QString sourcePath;

    /** Shell command bound to `button`, or an empty string when unbound. */
    QString commandFor(StylusButton button) const;

    /** ~/.config/stylus-popup/config.ini, or $STYLUS_POPUP_CONFIG when set. */
    static QString defaultPath();

    /** Loads the config, writing a default file when none exists yet. */
    static ButtonMapConfig load(const QString &path = QString());
};

/**
 * Watches the Bluetooth HID input nodes the stylus exposes and turns their
 * PAGE_UP / PAGE_DOWN key presses into `buttonPressed`.
 *
 * A node is the pen when its evdev name is one of the two names the supported
 * Xiaomi pens publish *and* it carries PAGE_UP or PAGE_DOWN. The capability
 * half keeps the pen's second, absolute-position-only node out; the exact name
 * keeps the i2c digitizer and ordinary keyboards out.
 *
 * The nodes are exclusive-grabbed (`EVIOCGRAB`) so the key stroke is swallowed
 * instead of reaching the focused application, and `poll()`ed so an absent pen
 * costs nothing. Nodes appearing or disappearing - which is how the pen
 * connects and disconnects - are picked up via inotify plus a slow re-scan.
 */
class StylusButtonMonitor : public QThread
{
    Q_OBJECT

public:
    explicit StylusButtonMonitor(ButtonMapConfig config, QObject *parent = nullptr);
    ~StylusButtonMonitor() override;

    void stop();

    /** Prints the input nodes this monitor would grab - `--list-input`. */
    static void printMatchingDevices(const ButtonMapConfig &config);

signals:
    void buttonPressed(StylusButton button, const QString &deviceName);

protected:
    void run() override;

private:
    /** One /dev/input/eventN node, and what we know about it. */
    struct Node {
        QString path;
        QString name;
        QString uniq;  // shown to the user, never matched on
        int     fd          = -1;
        int     openError   = 0;
        bool    grabbed     = false;
        bool    nameMatch   = false;
        bool    hasPageUp   = false;
        bool    hasPageDown = false;

        /** A node is ours when it is the pen *and* carries a mapped key. */
        bool matches() const { return nameMatch && (hasPageUp || hasPageDown); }
    };

    /** Opens `path` and fills `node`; false only when it cannot be opened.
     *  On success the descriptor stays open for the caller to grab/close. */
    static bool inspect(const QString &path, Node *node);

    void scan(std::vector<Node> &nodes);
    void attach(std::vector<Node> &nodes, const Node &node);
    bool drain(const Node &node, bool dispatch);
    void closeNode(Node &node);

    static constexpr int kRescanMs = 2000;

    ButtonMapConfig m_config;

    std::atomic<bool> m_running{true};
    int               m_wakeFd{-1};
    bool              m_openWarningLogged{false};
};

/**
 * Glue between the monitor and the configured shell commands: runs the command
 * bound to a button through `/bin/sh -c` and exports `STYLUS_BUTTON` so a single
 * command can tell the two buttons apart.
 */
class StylusButtonMapper : public QObject
{
    Q_OBJECT

public:
    explicit StylusButtonMapper(QObject *parent = nullptr);
    ~StylusButtonMapper() override;

    void start();

private slots:
    void onButtonPressed(StylusButton button, const QString &deviceName);

private:
    ButtonMapConfig      m_config;
    StylusButtonMonitor *m_monitor;
};
