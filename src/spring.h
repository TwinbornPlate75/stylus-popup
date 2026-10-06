#pragma once

#include <QtGlobal>
#include <cmath>

/**
 * A damped harmonic spring, the motion primitive behind every animated value
 * of the island. Unlike a fixed-duration easing curve, a spring keeps its
 * velocity when its target moves mid-flight, so an interrupted transition
 * (the pen connecting while the island is still growing) bends smoothly into
 * the new one instead of restarting.
 *
 * The damping ratio decides the character: below 1 the value overshoots and
 * settles (used for growing shapes), at 1 it arrives without bounce (used for
 * fades and for shrinking away).
 */
struct Spring {
    qreal value     = 0.0;
    qreal velocity  = 0.0;
    qreal target    = 0.0;
    qreal stiffness = 380.0;
    qreal damping   = 26.0;

    /** Distance and speed below which the spring counts as at rest. */
    qreal epsilon   = 0.5;

    void configure(qreal k, qreal c)
    {
        stiffness = k;
        damping   = c;
    }

    /** Advances by `dt` seconds. Large gaps (a stalled frame) are capped so
     *  the spring never jumps, and sub-stepped so stiff springs stay stable. */
    void step(qreal dt)
    {
        dt = qBound(0.0, dt, 1.0 / 30.0);
        const int steps = qMax(1, static_cast<int>(std::ceil(dt * 240.0)));
        const qreal h = dt / steps;
        for (int i = 0; i < steps; ++i) {
            const qreal accel = -stiffness * (value - target) - damping * velocity;
            velocity += accel * h;   /* semi-implicit Euler: velocity first */
            value    += velocity * h;
        }
        if (settled()) {
            value    = target;
            velocity = 0.0;
        }
    }

    bool settled() const
    {
        return qAbs(value - target) < epsilon && qAbs(velocity) < epsilon;
    }

    /** Jumps straight to `v` and stays there. */
    void snap(qreal v)
    {
        value    = v;
        target   = v;
        velocity = 0.0;
    }
};
