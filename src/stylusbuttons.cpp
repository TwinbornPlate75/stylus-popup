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

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

constexpr char kInputDir[]     = "/dev/input";
constexpr char kButtonsGroup[] = "buttons";

/* The pen reports its two side buttons as ordinary keyboard keys over
 * Bluetooth HID. */
constexpr int kPageUpKey   = KEY_PAGEUP;
constexpr int kPageDownKey = KEY_PAGEDOWN;

/* Bluetooth name of the supported pens. Both generations of the Xiaomi
 * Stylus Pen advertise the same one, and the kernel derives both evdev node
 * names from it: "<name> Keyboard" carries the side buttons, "<name>" is the
 * pen's bare pointer/digitizer node. */
constexpr char kPenBleName[]        = "Xiaomi Smart Pen";
constexpr char kPenKeyboardSuffix[] = " Keyboard";

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
           "# keyboard keys. stylus-popup grabs them so they no longer reach the\n"
           "# focused window, and runs the matching command through /bin/sh -c with\n"
           "# STYLUS_BUTTON exported as \"page-up\" or \"page-down\".\n"
           "\n"
           "[buttons]\n"
           "# Set to false to leave the buttons alone.\n"
           "enabled=true\n"
           "# Grab the input node exclusively, swallowing the key stroke.\n"
           "grab=true\n"
           "# Also fire the command on key auto-repeat while the button is held.\n"
           "repeat=false\n"
           "\n"
           "page-up=" << config.pageUpCommand << "\n"
           "page-down=" << config.pageDownCommand << "\n";
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
    return button == StylusButton::PageUp ? QStringLiteral("page-up") : QStringLiteral("page-down");
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

QString ButtonMapConfig::commandFor(StylusButton button) const
{
    return button == StylusButton::PageUp ? pageUpCommand : pageDownCommand;
}

ButtonMapConfig ButtonMapConfig::load(const QString &path)
{
    ButtonMapConfig config;
    config.sourcePath = path.isEmpty() ? defaultPath() : path;

    const bool existed = QFileInfo::exists(config.sourcePath);

    if (existed) {
        QSettings settings(config.sourcePath, QSettings::IniFormat);
        settings.beginGroup(kButtonsGroup);
        config.enabled         = settings.value("enabled",  config.enabled).toBool();
        config.grab            = settings.value("grab",     config.grab).toBool();
        config.repeat          = settings.value("repeat",   config.repeat).toBool();
        config.pageUpCommand   = iniText(settings, "page-up",   config.pageUpCommand);
        config.pageDownCommand = iniText(settings, "page-down", config.pageDownCommand);
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
        node->hasPageUp   = evdevSupportsKey(node->fd, kPageUpKey);
        node->hasPageDown = evdevSupportsKey(node->fd, kPageDownKey);
    }
    return true;
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

        const int rc = ::poll(pfds.data(), pfds.size(), kRescanMs);
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

bool StylusButtonMonitor::drain(const Node &node, bool dispatch)
{
    struct input_event ev;
    for (;;) {
        const ssize_t n = ::read(node.fd, &ev, sizeof ev);
        if (n == static_cast<ssize_t>(sizeof ev)) {
            if (dispatch && ev.type == EV_KEY && ev.value != 0
                && (ev.value != 2 || m_config.repeat)) {
                if (ev.code == kPageUpKey)
                    emit buttonPressed(StylusButton::PageUp, node.name);
                else if (ev.code == kPageDownKey)
                    emit buttonPressed(StylusButton::PageDown, node.name);
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
    connect(m_monitor, &StylusButtonMonitor::buttonPressed,
            this, &StylusButtonMapper::onButtonPressed,
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

void StylusButtonMapper::onButtonPressed(StylusButton button, const QString &deviceName)
{
    const QString name    = stylusButtonName(button);
    const QString command = m_config.commandFor(button);

    if (command.isEmpty()) {
        qInfo("StylusButtonMapper: %s pressed on \"%s\", but no command is bound",
              qPrintable(name), qPrintable(deviceName));
        return;
    }

    // STYLUS_BUTTON lets a single command serve both buttons.
    const QString script = QStringLiteral("export STYLUS_BUTTON=%1; %2").arg(name, command);

    if (QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script}))
        qInfo("StylusButtonMapper: %s -> %s", qPrintable(name), qPrintable(command));
    else
        qWarning("StylusButtonMapper: failed to run the command bound to %s", qPrintable(name));
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
        << "  pen name      : " << kPenBleName << "\n"
        << "  page-up       : " << config.pageUpCommand << "\n"
        << "  page-down     : " << config.pageDownCommand << "\n\n";

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
                << "  keys:"
                << (node.hasPageUp   ? QStringLiteral(" page-up")   : QString())
                << (node.hasPageDown ? QStringLiteral(" page-down") : QString()) << "\n";
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
