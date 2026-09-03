#include "colortheme.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSettings>

static QColor parseRgb(const QStringList &parts, const QColor &fallback)
{
    if (parts.size() != 3)
        return fallback;
    QColor c(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
    return c.isValid() ? c : fallback;
}

ColorTheme::ColorTheme(QObject *parent)
    : QObject(parent)
    , m_surface("#211F26")
    , m_onSurface("#E6E1E5")
    , m_onSurfaceVariant("#CAC4D0")
    , m_primary("#D0BCFF")
    , m_progressTrack("#49454F")
    , m_lowBattery("#FF6E6E")
    , m_charging("#79FFC1")
{
}

QStringList ColorTheme::candidatePaths() const
{
    QStringList paths;
    paths << QDir::home().filePath(".config/qt6ct/colors/matugen.conf");

    /* Qt5 fallback: qt5ct uses the same key layout as qt6ct. */
    paths << QDir::home().filePath(".config/qt5ct/colors/matugen.conf");

    /* Quickshell DMS matugen template. */
    paths << QDir::home().filePath(".local/share/color-schemes/matugen.conf");

    return paths;
}

bool ColorTheme::loadFromQt6ct()
{
    const QStringList paths = candidatePaths();

    for (const QString &path : paths) {
        if (!QFile::exists(path))
            continue;

        QSettings s(path, QSettings::IniFormat);

        if (s.status() != QSettings::NoError
            || s.value("Colors:Window/BackgroundNormal").toStringList().size() != 3) {
            qWarning("colortheme: config not in expected format: %s", qPrintable(path));
            continue;
        }

        loadFrom(path);
        m_path = path;
        qDebug("colortheme: loaded — surface=%s primary=%s (from %s)",
               qPrintable(m_surface.name()), qPrintable(m_primary.name()),
               qPrintable(path));
        return true;
    }

    qWarning("colortheme: no matugen color config found; using fallback theme colors");
    m_path.clear();
    return false;
}

void ColorTheme::loadFrom(const QString &path)
{
    QSettings s(path, QSettings::IniFormat);

    auto get = [&](const QString &key, const QColor &def) {
        /* QSettings IniFormat auto-splits comma-separated values into QStringList */
        return parseRgb(s.value(key).toStringList(), def);
    };

    m_surface          = get("Colors:Window/BackgroundNormal",    m_surface);
    m_onSurface        = get("Colors:Window/ForegroundNormal",    m_onSurface);
    m_onSurfaceVariant = get("Colors:Window/ForegroundInactive",  m_onSurfaceVariant);
    m_primary          = get("Colors:Button/DecorationFocus",     m_primary);
    m_progressTrack    = get("Colors:View/BackgroundAlternate",   m_progressTrack);
    m_lowBattery       = get("Colors:Button/BackgroundNegative",  m_lowBattery);
    m_charging         = get("Colors:Button/BackgroundPositive",  m_charging);
}
