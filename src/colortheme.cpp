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

ColorTheme::ColorTheme()
    : m_surface("#211F26")
    , m_onSurface("#E6E1E5")
    , m_onSurfaceVariant("#CAC4D0")
    , m_primary("#D0BCFF")
    , m_progressTrack("#49454F")
{
}

bool ColorTheme::loadFromQt6ct()
{
    const QString path = QDir::home().filePath(".config/qt6ct/colors/matugen.conf");
    if (!QFile::exists(path)) {
        qWarning("colortheme: qt6ct config not found: %s", qPrintable(path));
        return false;
    }

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

    qDebug("colortheme: loaded — surface=%s primary=%s",
           qPrintable(m_surface.name()), qPrintable(m_primary.name()));
    return true;
}
