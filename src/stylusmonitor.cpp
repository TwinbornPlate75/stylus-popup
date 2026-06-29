#include "stylusmonitor.h"

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>

struct Idtp9418Event {
    uint8_t soc;
    uint8_t is_charging;
    uint8_t is_attached;
    uint8_t charge_limit;
    uint8_t pen_mac[6];
    uint8_t state;
};

static QString formatMac(const uint8_t mac[6])
{
    // The IDTP9418 kernel driver reports MAC bytes in the opposite order
    // from what BlueZ uses, so reverse them here.
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(mac[5], 2, 16, QChar('0'))
        .arg(mac[4], 2, 16, QChar('0'))
        .arg(mac[3], 2, 16, QChar('0'))
        .arg(mac[2], 2, 16, QChar('0'))
        .arg(mac[1], 2, 16, QChar('0'))
        .arg(mac[0], 2, 16, QChar('0'))
        .toUpper();
}

static bool isMacNonZero(const uint8_t mac[6])
{
    for (int i = 0; i < 6; ++i) {
        if (mac[i] != 0)
            return true;
    }
    return false;
}

static bool readState(int fd, StylusState *out)
{
    Idtp9418Event evt;
    ssize_t n;
    do {
        n = read(fd, &evt, sizeof(evt));
    } while (n < 0 && errno == EINTR);

    if (n != sizeof(evt))
        return false;

    *out = {
        evt.is_attached != 0,
        evt.is_charging != 0,
        qMin(static_cast<int>(evt.soc), 100),
        static_cast<int>(evt.charge_limit),
        formatMac(evt.pen_mac),
        isMacNonZero(evt.pen_mac),
        evt.state == static_cast<uint8_t>(StylusPhase::Attaching)
            ? StylusPhase::Attaching
            : StylusPhase::Complete
    };
    return true;
}

StylusMonitor::StylusMonitor(QObject *parent) : QThread(parent) {}

StylusMonitor::~StylusMonitor()
{
    stop();
    wait();
}

void StylusMonitor::stop()
{
    m_running.store(false, std::memory_order_release);
    int fd = m_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0)
        close(fd);
}

void StylusMonitor::run()
{
    int fd = open("/dev/idtp9418", O_RDONLY);
    if (fd < 0)
        return;

    m_fd.store(fd, std::memory_order_release);

    while (m_running.load(std::memory_order_acquire)) {
        StylusState cur;
        if (!readState(fd, &cur))
            break;
        emit stateChanged(cur);
    }

    fd = m_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0)
        close(fd);
}
