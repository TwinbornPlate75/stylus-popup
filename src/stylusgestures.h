#pragma once

#include <QString>

#include <cstdint>
#include <optional>

/** The gestures one pen button can produce. */
enum class StylusGesture {
    Single,
    DoubleClick,
    LongPress
};

/** Stable identifier of a gesture, also exported as STYLUS_GESTURE. */
QString stylusGestureName(StylusGesture gesture);

/** Gesture timing thresholds, in milliseconds. */
struct GestureTiming {
    int doubleClickMs = 300;  /**< how long a click waits for a second one */
    int longPressMs   = 500;  /**< how long a press has to be held */
};

/**
 * Turns the raw press / release / auto-repeat events of a single button into
 * gestures.
 *
 * A press held for `longPressMs` fires LongPress right away, while the button
 * is still down, and releases nothing on the way up. A press released earlier
 * counts as a click, which is held back for `doubleClickMs` so that a second
 * press inside that window turns it into DoubleClick instead: the wait is
 * unconditional, since a click can only be told from a double click by seeing
 * whether another one follows.
 *
 * Two invariants keep the mapping predictable: one press produces at most one
 * gesture (the second press of a double click never also fires LongPress), and
 * auto-repeat re-runs whichever gesture the hold already resolved to.
 *
 * The detector does not know which gestures have a command bound - an empty
 * command is the mapping's business, and simply runs nothing. Keeping the
 * timing independent of that is what makes the class pure logic on a
 * caller-supplied clock, so every edge can be exercised without a device.
 */
class ButtonGestureDetector
{
public:
    void configure(GestureTiming timing);

    /** Feeds one evdev key event; 1 = press, 0 = release, 2 = auto-repeat. */
    std::optional<StylusGesture> feed(int value, int64_t nowMs);

    /** Flushes the deadlines that have passed; call whenever poll() times out. */
    std::optional<StylusGesture> tick(int64_t nowMs);

    /** Absolute time of the next internal deadline on the same clock, or -1. */
    int64_t deadlineMs() const;

private:
    std::optional<StylusGesture> press(int64_t nowMs);
    std::optional<StylusGesture> release(int64_t nowMs);
    std::optional<StylusGesture> repeat(int64_t nowMs);

    GestureTiming m_timing;

    bool    m_down          = false;
    bool    m_longFired     = false;
    bool    m_doubleFired   = false;
    int64_t m_pressedAt     = 0;
    int64_t m_pendingSingle = -1;  /**< release time of a click awaiting a 2nd one */
};
