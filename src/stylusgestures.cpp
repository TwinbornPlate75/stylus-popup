#include "stylusgestures.h"

QString stylusGestureName(StylusGesture gesture)
{
    switch (gesture) {
    case StylusGesture::Single:      return QStringLiteral("single");
    case StylusGesture::DoubleClick: return QStringLiteral("double-click");
    case StylusGesture::LongPress:   return QStringLiteral("long-press");
    }
    return QString();
}

void ButtonGestureDetector::configure(GestureTiming timing)
{
    m_timing = timing;

    // A reconfigured detector starts from a clean slate.
    m_down          = false;
    m_longFired     = false;
    m_doubleFired   = false;
    m_pressedAt     = 0;
    m_pendingSingle = -1;
}

std::optional<StylusGesture> ButtonGestureDetector::feed(int value, int64_t nowMs)
{
    switch (value) {
    case 0:  return release(nowMs);
    case 1:  return press(nowMs);
    default: return repeat(nowMs);
    }
}

std::optional<StylusGesture> ButtonGestureDetector::press(int64_t nowMs)
{
    // A pending single means this is the second click of a double click.
    const bool secondClick = m_pendingSingle >= 0;

    m_pendingSingle = -1;
    m_down          = true;
    m_pressedAt     = nowMs;
    m_longFired     = false;
    m_doubleFired   = secondClick;

    return secondClick ? std::optional<StylusGesture>(StylusGesture::DoubleClick)
                       : std::nullopt;
}

std::optional<StylusGesture> ButtonGestureDetector::release(int64_t nowMs)
{
    if (!m_down)
        return std::nullopt;  // a release without a press we saw

    const bool doubleFired = m_doubleFired;
    const bool longFired   = m_longFired;
    const bool heldLong    = (nowMs - m_pressedAt) >= m_timing.longPressMs;

    m_down        = false;
    m_doubleFired = false;

    if (doubleFired || longFired)
        return std::nullopt;  // this press already produced its gesture

    if (heldLong) {
        m_longFired = true;  // the deadline passed without a tick noticing
        return StylusGesture::LongPress;
    }

    // Hold the click back: a second press within the window means double.
    m_pendingSingle = nowMs;
    return std::nullopt;
}

std::optional<StylusGesture> ButtonGestureDetector::repeat(int64_t nowMs)
{
    if (!m_down || m_doubleFired)
        return std::nullopt;

    if (m_longFired)
        return StylusGesture::LongPress;  // the hold already resolved to it

    if ((nowMs - m_pressedAt) >= m_timing.longPressMs) {
        m_longFired = true;
        return StylusGesture::LongPress;
    }

    return StylusGesture::Single;
}

std::optional<StylusGesture> ButtonGestureDetector::tick(int64_t nowMs)
{
    if (m_down) {
        if (!m_doubleFired && !m_longFired
            && (nowMs - m_pressedAt) >= m_timing.longPressMs) {
            m_longFired = true;
            return StylusGesture::LongPress;
        }
        return std::nullopt;
    }

    if (m_pendingSingle >= 0 && (nowMs - m_pendingSingle) >= m_timing.doubleClickMs) {
        m_pendingSingle = -1;
        return StylusGesture::Single;
    }

    return std::nullopt;
}

int64_t ButtonGestureDetector::deadlineMs() const
{
    int64_t deadline = -1;

    if (m_down && !m_doubleFired && !m_longFired)
        deadline = m_pressedAt + m_timing.longPressMs;

    if (m_pendingSingle >= 0) {
        const int64_t single = m_pendingSingle + m_timing.doubleClickMs;
        if (deadline < 0 || single < deadline)
            deadline = single;
    }

    return deadline;
}
