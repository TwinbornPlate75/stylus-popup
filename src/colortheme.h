#pragma once

#include <QColor>
#include <QObject>
#include <QStringList>

class ColorTheme : public QObject
{
    Q_OBJECT

public:
    explicit ColorTheme(QObject *parent = nullptr);

    bool loadFromQt6ct();

    const QColor &surface()          const { return m_surface; }
    const QColor &onSurface()        const { return m_onSurface; }
    const QColor &onSurfaceVariant() const { return m_onSurfaceVariant; }
    const QColor &primary()          const { return m_primary; }
    const QColor &progressTrack()    const { return m_progressTrack; }
    const QColor &lowBattery()       const { return m_lowBattery; }
    const QColor &charging()         const { return m_charging; }
    const QColor &warning()          const { return m_warning; }

    /* Derived roles, recomputed whenever the palette is (re)loaded. */
    const QColor &islandFill()       const { return m_islandFill; }
    const QColor &outline()          const { return m_outline; }
    const QColor &error()            const { return m_lowBattery; }

private:
    QStringList candidatePaths() const;
    void loadFrom(const QString &path);
    void deriveRoles();

    QColor m_surface;
    QColor m_onSurface;
    QColor m_onSurfaceVariant;
    QColor m_primary;
    QColor m_progressTrack;
    QColor m_lowBattery;
    QColor m_charging;
    QColor m_warning;
    QColor m_islandFill;
    QColor m_outline;
};
