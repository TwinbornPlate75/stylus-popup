#pragma once

#include <QThread>
#include <QString>
#include <atomic>

enum class StylusPhase {
    Attaching = 1,
    Complete  = 2
};

struct StylusState {
    bool        attached  = false;
    bool        charging  = false;
    int         capacity  = 0;
    int         limit     = 85;
    QString     macAddress;
    bool        macValid  = false;
    StylusPhase phase     = StylusPhase::Complete;

    bool operator==(const StylusState &other) const {
        return attached == other.attached
            && charging == other.charging
            && capacity == other.capacity
            && limit == other.limit
            && macAddress == other.macAddress
            && macValid == other.macValid
            && phase == other.phase;
    }

    bool operator!=(const StylusState &other) const {
        return !(*this == other);
    }
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
    std::atomic<bool> m_running{true};
    std::atomic<int>  m_fd{-1};
};
