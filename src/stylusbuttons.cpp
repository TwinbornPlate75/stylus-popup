#include "stylusbuttons.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

constexpr char kInputDir[]     = "/dev/input";
constexpr char kButtonsGroup[] = "buttons";

/* Which key code each side button reports. The main button sends PAGE_DOWN
 * and the secondary one PAGE_UP - the opposite of what those key names suggest,
 * which is why the buttons are named after their role and the key codes are
 * mentioned only here. */
constexpr int kPrimaryKey   = KEY_PAGEDOWN;
constexpr int kSecondaryKey = KEY_PAGEUP;

/* Bluetooth name of the supported pens. Both generations of the Xiaomi
 * Stylus Pen advertise the same one, and the kernel derives both evdev node
 * names from it: "<name> Keyboard" carries the side buttons, "<name>" is the
 * pen's bare pointer/digitizer node. */
constexpr char kPenBleName[]        = "Xiaomi Smart Pen";
constexpr char kPenKeyboardSuffix[] = " Keyboard";

/* Gesture thresholds are clamped: a typo must not make the mapping unusable
 * (0 ms would let every click be swallowed as a long press, a huge value would
 * delay every click into the next session). */
constexpr int kMinGestureMs = 50;
constexpr int kMaxGestureMs = 10000;

/** True for the two evdev node names the supported pens produce. */
bool isStylusNodeName(const QString &name)
{
    if (name.isEmpty())
        return false;

    const QString base     = QString::fromLatin1(kPenBleName);
    const QString keyboard = base + QString::fromLatin1(kPenKeyboardSuffix);
    return name.compare(base, Qt::CaseInsensitive) == 0
        || name.compare(keyboard, Qt::CaseInsensitive) == 0;
}

/**
 * Reads a config value as text. QSettings hands back a QStringList as soon as
 * the value contains a comma, and toString() on such a value is empty - which
 * would silently drop a command like "foo --a,b". Re-joining the list
 * restores the text verbatim.
 */
QString iniText(const QSettings &settings, const QString &key, const QString &fallback)
{
    const QVariant value = settings.value(key);
    if (!value.isValid())
        return fallback;

    const QStringList parts = value.toStringList();
    return parts.size() > 1 ? parts.join(QLatin1Char(',')) : value.toString();
}

/** Reads a gesture threshold in ms; an unparsable value keeps the default. */
int iniMilliseconds(const QSettings &settings, const QString &key, int fallback)
{
    bool ok = false;
    const int value = settings.value(key).toInt(&ok);
    return ok ? qBound(kMinGestureMs, value, kMaxGestureMs) : fallback;
}

/** An empty command means "gesture disabled" - make that visible in the output. */
QString orUnbound(const QString &command)
{
    return command.isEmpty() ? QStringLiteral("(unbound)") : command;
}

/**
 * Writes the shipped defaults with a short explanation, so the mapping can be
 * discovered and edited without reading the source.
 */
bool writeDefaultConfig(const ButtonMapConfig &config)
{
    const QFileInfo info(config.sourcePath);
    if (!QDir().mkpath(info.absolutePath())) {
        qWarning("StylusButtonMapper: cannot create %s", qPrintable(info.absolutePath()));
        return false;
    }

    QFile file(config.sourcePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("StylusButtonMapper: cannot write %s: %s",
                 qPrintable(config.sourcePath), qPrintable(file.errorString()));
        return false;
    }

    QTextStream out(&file);
    out << "# stylus-popup button mapping.\n"
           "#\n"
           "# Both side buttons of the stylus arrive over Bluetooth HID as ordinary\n"
           "# keyboard keys: the main button sends PAGE_DOWN, the secondary one\n"
           "# PAGE_UP. They are named after the pen, not after the key, so\n"
           "# \"primary\" is the main button and \"secondary\" the other one.\n"
           "# stylus-popup grabs them so they no longer reach the focused window,\n"
           "# and runs the matching command through /bin/sh -c with STYLUS_BUTTON\n"
           "# (\"primary\" or \"secondary\") and STYLUS_GESTURE\n"
           "# (\"single\", \"double-click\" or \"long-press\") exported.\n"
           "#\n"
           "# Each button has three gestures: <button> is a single click,\n"
           "# <button>-double-click is a second click within double-click-ms, and\n"
           "# <button>-long-press is a press held for long-press-ms. One press\n"
           "# produces at most one gesture. An empty command disables that gesture.\n"
           "# Whether a click is the first of two can only be known once the click\n"
           "# window has passed, so every click pays double-click-ms.\n"
           "\n"
           "[buttons]\n"
           "# Set to false to leave the buttons alone.\n"
           "enabled=true\n"
           "# Grab the input node exclusively, swallowing the key stroke.\n"
           "grab=true\n"
           "# Also fire the mapped command on key auto-repeat while the button is\n"
           "# held: the repeat re-runs the gesture the hold resolved to.\n"
           "repeat=false\n"
           "# Click window, and how long a press has to be held.\n"
           "double-click-ms=" << config.doubleClickMs << "\n"
           "long-press-ms=" << config.longPressMs << "\n"
           "\n"
           "primary=" << config.primaryCommand << "\n"
           "primary-double-click=" << config.primaryDouble << "\n"
           "primary-long-press=" << config.primaryLong << "\n"
           "secondary=" << config.secondaryCommand << "\n"
           "secondary-double-click=" << config.secondaryDouble << "\n"
           "secondary-long-press=" << config.secondaryLong << "\n";
    file.close();

    qInfo("StylusButtonMapper: wrote default button mapping to %s",
          qPrintable(config.sourcePath));
    return true;
}

/** Names of the evdev nodes under /dev/input, sorted. */
QStringList inputEventPaths()
{
    QStringList paths;

    DIR *dir = ::opendir(kInputDir);
    if (!dir)
        return paths;

    while (struct dirent *entry = ::readdir(dir)) {
        const char *name = entry->d_name;
        if (std::strncmp(name, "event", 5) != 0)
            continue;

        bool numeric = name[5] != '\0';
        for (const char *c = name + 5; *c; ++c) {
            if (!std::isdigit(static_cast<unsigned char>(*c))) {
                numeric = false;
                break;
            }
        }
        if (!numeric)
            continue;

        paths.append(QString::fromLatin1(kInputDir) + QLatin1Char('/') + QString::fromLatin1(name));
    }

    ::closedir(dir);
    paths.sort();
    return paths;
}

/** EVIOCGNAME encodes the buffer length in the request itself. */
QString evdevName(int fd)
{
    char buf[256] = {};
    if (::ioctl(fd, EVIOCGNAME(sizeof buf), buf) < 0)
        return QString();
    buf[sizeof buf - 1] = '\0';  // the kernel truncates without terminating
    return QString::fromLocal8Bit(buf);
}

/** Bluetooth address of a HID device, empty when the device has none. */
QString evdevUniq(int fd)
{
    char buf[64] = {};
    if (::ioctl(fd, EVIOCGUNIQ(sizeof buf), buf) < 0)
        return QString();
    buf[sizeof buf - 1] = '\0';
    return QString::fromLocal8Bit(buf);
}

bool evdevSupportsKey(int fd, int code)
{
    unsigned long bits[(KEY_MAX / (8 * sizeof(unsigned long))) + 1] = {};
    if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) < 0)
        return false;
    const unsigned long word = bits[code / (8 * sizeof(unsigned long))];
    return (word & (1UL << (code % (8 * sizeof(unsigned long))))) != 0;
}

const char *yesNo(bool value) { return value ? "yes" : "no"; }

} // namespace

QString stylusButtonName(StylusButton button)
{
    return button == StylusButton::Primary ? QStringLiteral("primary")
                                           : QStringLiteral("secondary");
}

/* ── ButtonMapConfig ─────────────────────────────────────────────────────── */

QString ButtonMapConfig::defaultPath()
{
    const QByteArray override = qgetenv("STYLUS_POPUP_CONFIG");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);

    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/stylus-popup/config.ini");
}

QString ButtonMapConfig::commandFor(StylusButton button, StylusGesture gesture) const
{
    switch (gesture) {
    case StylusGesture::Single:
        return button == StylusButton::Primary ? primaryCommand : secondaryCommand;
    case StylusGesture::DoubleClick:
        return button == StylusButton::Primary ? primaryDouble : secondaryDouble;
    case StylusGesture::LongPress:
        return button == StylusButton::Primary ? primaryLong : secondaryLong;
    }
    return QString();
}

ButtonMapConfig ButtonMapConfig::load(const QString &path)
{
    ButtonMapConfig config;
    config.sourcePath = path.isEmpty() ? defaultPath() : path;

    const bool existed = QFileInfo::exists(config.sourcePath);

    if (existed) {
        QSettings settings(config.sourcePath, QSettings::IniFormat);
        settings.beginGroup(kButtonsGroup);
        config.enabled         = settings.value("enabled", config.enabled).toBool();
        config.grab            = settings.value("grab",    config.grab).toBool();
        config.repeat          = settings.value("repeat",  config.repeat).toBool();
        config.doubleClickMs   = iniMilliseconds(settings, "double-click-ms", config.doubleClickMs);
        config.longPressMs     = iniMilliseconds(settings, "long-press-ms", config.longPressMs);
        config.primaryCommand   = iniText(settings, "primary", config.primaryCommand);
        config.primaryDouble    = iniText(settings, "primary-double-click", config.primaryDouble);
        config.primaryLong      = iniText(settings, "primary-long-press", config.primaryLong);
        config.secondaryCommand = iniText(settings, "secondary", config.secondaryCommand);
        config.secondaryDouble  = iniText(settings, "secondary-double-click", config.secondaryDouble);
        config.secondaryLong    = iniText(settings, "secondary-long-press", config.secondaryLong);
        settings.endGroup();

        qInfo("StylusButtonMapper: loaded button mapping from %s", qPrintable(config.sourcePath));
    } else if (!writeDefaultConfig(config)) {
        qWarning("StylusButtonMapper: falling back to built-in button mapping defaults");
    }

    return config;
}

/* ── StylusButtonMonitor ─────────────────────────────────────────────────── */

bool StylusButtonMonitor::inspect(const QString &path, Node *node)
{
    node->path = path;
    node->fd   = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (node->fd < 0) {
        node->openError = errno;
        return false;
    }

    node->name = evdevName(node->fd);
    node->uniq = evdevUniq(node->fd);

    node->nameMatch = isStylusNodeName(node->name);

    if (node->nameMatch) {
        node->hasPrimary   = evdevSupportsKey(node->fd, kPrimaryKey);
        node->hasSecondary = evdevSupportsKey(node->fd, kSecondaryKey);
    }
    return true;
}

ButtonGestureDetector &StylusButtonMonitor::detectorFor(Node &node, StylusButton button)
{
    return button == StylusButton::Primary ? node.primaryDetector : node.secondaryDetector;
}

int64_t StylusButtonMonitor::monotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

StylusButtonMonitor::StylusButtonMonitor(ButtonMapConfig config, QObject *parent)
    : QThread(parent)
    , m_config(std::move(config))
{
    m_wakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wakeFd < 0)
        qWarning("StylusButtonMonitor: eventfd failed: %s", std::strerror(errno));
}

StylusButtonMonitor::~StylusButtonMonitor()
{
    stop();
    wait();
    if (m_wakeFd >= 0)
        ::close(m_wakeFd);
}

void StylusButtonMonitor::stop()
{
    m_running.store(false, std::memory_order_release);
    if (m_wakeFd >= 0) {
        const uint64_t one = 1;
        const ssize_t written = ::write(m_wakeFd, &one, sizeof one);
        Q_UNUSED(written);
    }
}

void StylusButtonMonitor::run()
{
    if (!m_config.enabled) {
        qInfo("StylusButtonMonitor: button mapping disabled in %s",
              qPrintable(m_config.sourcePath));
        return;
    }

    int inotifyFd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd >= 0) {
        const uint32_t mask = IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_ATTRIB;
        if (::inotify_add_watch(inotifyFd, kInputDir, mask) < 0)
            qDebug("StylusButtonMonitor: cannot watch %s: %s", kInputDir, std::strerror(errno));
    } else {
        qDebug("StylusButtonMonitor: inotify unavailable, relying on re-scans");
    }

    std::vector<Node> nodes;
    scan(nodes);
    if (nodes.empty())
        qInfo("StylusButtonMonitor: no stylus button device yet, waiting for the pen");

    while (m_running.load(std::memory_order_acquire)) {
        std::vector<struct pollfd> pfds;
        pfds.reserve(nodes.size() + 2);
        if (inotifyFd >= 0)
            pfds.push_back({inotifyFd, POLLIN, 0});
        for (const Node &node : nodes)
            pfds.push_back({node.fd, POLLIN, 0});
        const size_t wakeIndex = pfds.size();
        if (m_wakeFd >= 0)
            pfds.push_back({m_wakeFd, POLLIN, 0});

        // The poll timeout is the gesture timer: waking up at the next deadline
        // is what turns a held or half-finished gesture into its verdict.
        const int rc = ::poll(pfds.data(), pfds.size(), nextTimeoutMs(nodes));
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            qWarning("StylusButtonMonitor: poll failed: %s", std::strerror(errno));
            break;
        }

        bool rescan = (rc == 0);
        size_t index = 0;

        if (inotifyFd >= 0) {
            if (pfds[index].revents & POLLIN) {
                char buf[1024];
                while (::read(inotifyFd, buf, sizeof buf) > 0) {}
                rescan = true;
            }
            ++index;
        }

        // Collect dead nodes first: closing them while walking would invalidate
        // the pollfd-to-node mapping for the remaining entries.
        std::vector<size_t> dead;
        for (size_t n = 0; n < nodes.size(); ++n, ++index) {
            const short revents = pfds[index].revents;
            if (!revents)
                continue;
            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) || !drain(nodes[n], true))
                dead.push_back(n);
        }

        if (m_wakeFd >= 0 && (pfds[wakeIndex].revents & POLLIN)) {
            uint64_t drained = 0;
            while (::read(m_wakeFd, &drained, sizeof drained) > 0) {}
        }

        for (auto it = dead.rbegin(); it != dead.rend(); ++it) {
            closeNode(nodes[*it]);
            nodes.erase(nodes.begin() + static_cast<long>(*it));
            rescan = true;
        }

        if (rescan)
            scan(nodes);

        flushDueGestures(nodes);
    }

    for (Node &node : nodes)
        closeNode(node);
    if (inotifyFd >= 0)
        ::close(inotifyFd);
}

void StylusButtonMonitor::scan(std::vector<Node> &nodes)
{
    int failures = 0;
    QString firstFailure;
    int     firstError = 0;

    for (const QString &path : inputEventPaths()) {
        bool known = false;
        for (const Node &node : nodes) {
            if (node.path == path) {
                known = true;
                break;
            }
        }
        if (known)
            continue;

        Node node;
        if (!inspect(path, &node)) {
            if (failures++ == 0) {
                firstFailure = path;
                firstError   = node.openError;
            }
            continue;
        }

        if (!node.matches()) {
            ::close(node.fd);
            continue;
        }

        attach(nodes, node);
    }

    if (failures > 0 && nodes.empty() && !m_openWarningLogged) {
        m_openWarningLogged = true;
        qWarning("StylusButtonMonitor: cannot open %d input node(s), e.g. %s: %s - "
                 "is this user a member of the \"input\" group?",
                 failures, qPrintable(firstFailure), std::strerror(firstError));
    }
}

void StylusButtonMonitor::attach(std::vector<Node> &nodes, const Node &node)
{
    Node watched = node;

    const GestureTiming timing{m_config.doubleClickMs, m_config.longPressMs};
    watched.primaryDetector.configure(timing);
    watched.secondaryDetector.configure(timing);

    if (m_config.grab) {
        if (::ioctl(watched.fd, EVIOCGRAB, 1) == 0) {
            watched.grabbed = true;
        } else {
            qWarning("StylusButtonMonitor: cannot grab %s: %s - the command still runs, "
                     "but the key stroke keeps reaching the focused window",
                     qPrintable(watched.path), std::strerror(errno));
        }
    }

    // Whatever the kernel queued before the grab is stale by now.
    drain(watched, false);

    qInfo("StylusButtonMonitor: watching \"%s\" (%s) at %s%s",
          qPrintable(watched.name),
          watched.uniq.isEmpty() ? "no address" : qPrintable(watched.uniq),
          qPrintable(watched.path),
          watched.grabbed ? ", grabbed" : "");

    nodes.push_back(watched);
}

bool StylusButtonMonitor::drain(Node &node, bool dispatch)
{
    const int64_t now = monotonicMs();
    struct input_event ev;
    for (;;) {
        const ssize_t n = ::read(node.fd, &ev, sizeof ev);
        if (n == static_cast<ssize_t>(sizeof ev)) {
            if (dispatch && ev.type == EV_KEY) {
                if (ev.code == kPrimaryKey)
                    feed(node, StylusButton::Primary, ev.value, now);
                else if (ev.code == kSecondaryKey)
                    feed(node, StylusButton::Secondary, ev.value, now);
            }
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return true;
        return false;
    }
}

void StylusButtonMonitor::feed(Node &node, StylusButton button, int value, int64_t nowMs)
{
    // An unmapped auto-repeat is dropped here, but the end-of-loop deadline
    // flush still fires the long press it kept alive.
    if (value == 2 && !m_config.repeat)
        return;

    if (std::optional<StylusGesture> gesture = detectorFor(node, button).feed(value, nowMs))
        emit gestureTriggered(button, *gesture, node.name);
}

void StylusButtonMonitor::flushDueGestures(std::vector<Node> &nodes)
{
    const int64_t now = monotonicMs();

    for (Node &node : nodes) {
        const std::pair<StylusButton, ButtonGestureDetector *> detectors[] = {
            {StylusButton::Primary,   &node.primaryDetector},
            {StylusButton::Secondary, &node.secondaryDetector},
        };
        for (const auto &entry : detectors) {
            while (std::optional<StylusGesture> gesture = entry.second->tick(now))
                emit gestureTriggered(entry.first, *gesture, node.name);
        }
    }
}

int StylusButtonMonitor::nextTimeoutMs(const std::vector<Node> &nodes) const
{
    int64_t earliest = -1;

    for (const Node &node : nodes) {
        for (const ButtonGestureDetector *detector : {&node.primaryDetector, &node.secondaryDetector}) {
            const int64_t deadline = detector->deadlineMs();
            if (deadline >= 0 && (earliest < 0 || deadline < earliest))
                earliest = deadline;
        }
    }

    if (earliest < 0)
        return kRescanMs;

    const int64_t remaining = earliest - monotonicMs();
    if (remaining <= 0)
        return 0;

    return static_cast<int>(std::min<int64_t>(remaining, kRescanMs));
}

void StylusButtonMonitor::closeNode(Node &node)
{
    if (node.fd < 0)
        return;

    if (node.grabbed)
        ::ioctl(node.fd, EVIOCGRAB, 0);
    ::close(node.fd);

    qInfo("StylusButtonMonitor: released \"%s\" at %s",
          qPrintable(node.name), qPrintable(node.path));

    node.fd      = -1;
    node.grabbed = false;
}

/* ── StylusButtonMapper ──────────────────────────────────────────────────── */

StylusButtonMapper::StylusButtonMapper(QObject *parent)
    : QObject(parent)
    , m_config(ButtonMapConfig::load())
    , m_monitor(new StylusButtonMonitor(m_config, this))
{
    connect(m_monitor, &StylusButtonMonitor::gestureTriggered,
            this, &StylusButtonMapper::onGestureTriggered,
            Qt::QueuedConnection);
}

StylusButtonMapper::~StylusButtonMapper()
{
    m_monitor->stop();
    m_monitor->wait();
}

void StylusButtonMapper::start()
{
    if (!m_config.enabled) {
        qInfo("StylusButtonMapper: disabled, leaving the pen buttons untouched");
        return;
    }
    m_monitor->start();
}

void StylusButtonMapper::onGestureTriggered(StylusButton button, StylusGesture gesture,
                                            const QString &deviceName)
{
    const QString name    = stylusButtonName(button);
    const QString gestureName = stylusGestureName(gesture);
    const QString command = m_config.commandFor(button, gesture);

    if (command.isEmpty()) {
        qInfo("StylusButtonMapper: %s %s on \"%s\", but no command is bound",
              qPrintable(name), qPrintable(gestureName), qPrintable(deviceName));
        return;
    }

    // The two exports let a single command serve every button and gesture.
    const QString script = QStringLiteral("export STYLUS_BUTTON=%1 STYLUS_GESTURE=%2; %3")
                               .arg(name, gestureName, command);

    if (QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script}))
        qInfo("StylusButtonMapper: %s %s -> %s",
              qPrintable(name), qPrintable(gestureName), qPrintable(command));
    else
        qWarning("StylusButtonMapper: failed to run the command bound to %s %s",
                 qPrintable(name), qPrintable(gestureName));
}

/* ── Diagnostics ─────────────────────────────────────────────────────────── */

void StylusButtonMonitor::printMatchingDevices(const ButtonMapConfig &config)
{
    QTextStream out(stdout);
    out << "stylus-popup button mapping\n"
        << "  config        : "
        << (config.sourcePath.isEmpty() ? QStringLiteral("(built-in defaults)")
                                        : config.sourcePath) << "\n"
        << "  enabled       : " << yesNo(config.enabled) << "\n"
        << "  grab          : " << yesNo(config.grab) << "\n"
        << "  repeat        : " << yesNo(config.repeat) << "\n"
        << "  timing        : double-click " << config.doubleClickMs
        << " ms, long-press " << config.longPressMs << " ms\n"
        << "  pen name      : " << kPenBleName << "\n";

    for (StylusButton button : {StylusButton::Primary, StylusButton::Secondary}) {
        out << "  " << stylusButtonName(button) << "\n";
        for (StylusGesture gesture : {StylusGesture::Single,
                                      StylusGesture::DoubleClick,
                                      StylusGesture::LongPress}) {
            out << "    " << stylusGestureName(gesture).leftJustified(12)
                << ": " << orUnbound(config.commandFor(button, gesture)) << "\n";
        }
    }
    out << "\n";

    const QStringList paths = inputEventPaths();
    int watched  = 0;
    int examined = 0;

    for (const QString &path : paths) {
        Node node;
        if (!inspect(path, &node)) {
            out << "  [skip] " << path << "  cannot open: "
                << QString::fromLocal8Bit(std::strerror(node.openError))
                << " (member of the \"input\" group?)\n";
            continue;
        }

        ++examined;

        if (node.matches()) {
            ++watched;
            out << "  [grab] " << path << "  \"" << node.name << "\""
                << "  uniq=" << (node.uniq.isEmpty() ? QStringLiteral("-") : node.uniq)
                << "  buttons:"
                << (node.hasPrimary   ? QStringLiteral(" primary (PAGE_DOWN)") : QString())
                << (node.hasSecondary ? QStringLiteral(" secondary (PAGE_UP)") : QString())
                << "\n";
        } else if (node.nameMatch) {
            out << "  [skip] " << path << "  \"" << node.name
                << "\"  is the pen's node but exposes neither PAGE_UP nor PAGE_DOWN\n";
        } else {
            out << "  [skip] " << path << "  \"" << node.name << "\"  not a stylus node\n";
        }

        ::close(node.fd);
    }

    out << "\n" << examined << " of " << paths.size() << " event nodes examined, "
        << watched << " would be grabbed.\n";
    if (watched == 0)
        out << "No stylus button device found - is the pen connected over bluetooth?\n";
}
