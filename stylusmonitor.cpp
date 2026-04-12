#include "stylusmonitor.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cmath>

/* ioctl numbers — must match idtp9418.c exactly */
#define IDTP9418_IOC_MAGIC         'W'
#define IDTP9418_IOC_GET_CAPACITY  _IOR(IDTP9418_IOC_MAGIC, 0, int)
#define IDTP9418_IOC_GET_CHARGING  _IOR(IDTP9418_IOC_MAGIC, 1, int)
#define IDTP9418_IOC_GET_LIMIT     _IOR(IDTP9418_IOC_MAGIC, 2, int)
#define IDTP9418_IOC_GET_ATTACHED  _IOR(IDTP9418_IOC_MAGIC, 4, int)

static StylusState readState(int fd)
{
    StylusState s;
    int val = 0;
    if (ioctl(fd, IDTP9418_IOC_GET_ATTACHED,  &val) == 0) s.attached  = val != 0;
    if (ioctl(fd, IDTP9418_IOC_GET_CHARGING,  &val) == 0) s.charging  = val != 0;
    if (ioctl(fd, IDTP9418_IOC_GET_CAPACITY,  &val) == 0) s.capacity  = val;
    if (ioctl(fd, IDTP9418_IOC_GET_LIMIT,     &val) == 0) s.limit     = val;
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
    m_fd = open("/dev/idtp9418", O_RDONLY | O_NONBLOCK);
    if (m_fd < 0)
        return;

    StylusState prev;
    bool first = true;

    while (m_running) {
        /*
         * The driver's poll() always returns POLLIN immediately (it calls
         * poll_wait then returns POLLIN|POLLRDNORM unconditionally), so we
         * use a 500 ms timeout purely as a rate-limiter to avoid busy-looping.
         * wake_up(&wait_queue) in the kernel will also trigger our poll,
         * giving sub-500 ms responsiveness on state changes.
         */
        struct pollfd pfd = { m_fd, POLLIN, 0 };
        poll(&pfd, 1, 500);

        if (!m_running)
            break;

        StylusState cur = readState(m_fd);

        bool changed = first
            || cur.attached  != prev.attached
            || cur.charging  != prev.charging
            || std::abs(cur.capacity - prev.capacity) >= 1;

        if (changed) {
            prev  = cur;
            first = false;
            /* Only pop up if the pen is actually there */
            if (cur.attached)
                emit stateChanged(cur);
        }
    }

    if (m_fd >= 0) {
        close(m_fd);
        m_fd = -1;
    }
}
