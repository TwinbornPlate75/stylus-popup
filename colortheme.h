#pragma once

#include <QColor>

class ColorTheme
{
public:
    ColorTheme();

    bool loadFromQt6ct();

    const QColor &surface()          const { return m_surface; }
    const QColor &onSurface()        const { return m_onSurface; }
    const QColor &onSurfaceVariant() const { return m_onSurfaceVariant; }
    const QColor &primary()          const { return m_primary; }
    const QColor &progressTrack()    const { return m_progressTrack; }

private:
    QColor m_surface;
    QColor m_onSurface;
    QColor m_onSurfaceVariant;
    QColor m_primary;
    QColor m_progressTrack;
};
