#pragma once

#include <QColor>
#include <QString>

class ColorTheme
{
public:
    ColorTheme();

    bool loadFromQt6ct();

    QColor surface() const;
    QColor onSurface() const;
    QColor onSurfaceVariant() const;
    QColor primary() const;
    QColor progressTrack() const;

    bool isValid() const { return m_valid; }

private:
    QColor m_surface;
    QColor m_onSurface;
    QColor m_onSurfaceVariant;
    QColor m_primary;
    QColor m_progressTrack;
    bool m_valid = false;
};
