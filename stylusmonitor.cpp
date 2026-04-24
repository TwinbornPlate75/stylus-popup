#include "stylusmonitor.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cmath>

#pragma pack(push, 1)
struct Idtp9418Event {
    uint8_t soc;
    uint8_t is_charging;
    uint8_t is_attached;
    uint8_t charge_limit;
};
#pragma pack(pop)

static StylusState readState(int fd)
{
    Idtp9418Event evt;
    ssize_t n = read(fd, &evt, sizeof(evt));
    if (n != sizeof(evt))
        return {};

    StylusState s;
    s.attached  = evt.is_attached != 0;
    s.charging  = evt.is_charging != 0;
    s.capacity  = qMin(static_cast<int>(evt.soc), 100);
    s.limit     = static_cast<int>(evt.charge_limit);
    return s;
}

StylusMonitor::StylusMonitor(QObject *parent) : QThread(parent) {}

StylusMonitor::~StylusMonitor()
{
    stop();
    wait();
}

void StylusMonitor::stop()
{
    m_running = false;
    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}

void StylusMonitor::run()
{
    m_fd = open("/dev/idtp9418", O_RDONLY);
    if (m_fd < 0)
        return;

    StylusState prev;

    while (m_running) {
        StylusState cur = readState(m_fd);
        prev = cur;
        if (cur.attached)
            emit stateChanged(cur);
    }

    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}
