#pragma once

#include <QThread>

struct StylusState {
    bool attached  = false;
    bool charging  = false;
    int  capacity  = 0;
    int  limit     = 85;

    bool operator==(const StylusState &other) const {
        return attached == other.attached
            && charging == other.charging
            && capacity == other.capacity
            && limit == other.limit;
    }
    bool operator!=(const StylusState &other) const { return !(*this == other); }
};

class StylusMonitor : public QThread
{
    Q_OBJECT

public:
    explicit StylusMonitor(QObject *parent = nullptr);
    ~StylusMonitor() override;

    void stop();

signals:
    void stateChanged(StylusState state);

protected:
    void run() override;

private:
    volatile bool m_running = true;
    int           m_fd      = -1;
};
