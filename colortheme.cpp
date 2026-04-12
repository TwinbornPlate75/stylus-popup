#include "colortheme.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>

ColorTheme::ColorTheme() = default;

static QColor parseKdeColor(const QString &s)
{
    if (s.isEmpty())
        return QColor();
    const QStringList parts = s.split(',');
    if (parts.size() != 3)
        return QColor();
    bool ok1, ok2, ok3;
    const int r = parts[0].trimmed().toInt(&ok1);
    const int g = parts[1].trimmed().toInt(&ok2);
    const int b = parts[2].trimmed().toInt(&ok3);
    if (!ok1 || !ok2 || !ok3)
        return QColor();
    return QColor(r, g, b);
}

static QColor getColor(const QMap<QString, QMap<QString, QString>> &data,
                       const QString &group, const QString &key,
                       const QColor &fallback = QColor())
{
    auto git = data.find(group);
    if (git == data.end())
        return fallback;
    auto kit = git.value().find(key);
    if (kit == git.value().end())
        return fallback;
    QColor c = parseKdeColor(kit.value());
    return c.isValid() ? c : fallback;
}

bool ColorTheme::loadFromQt6ct()
{
    const QString path = QString::fromUtf8(qgetenv("HOME")) + "/.config/qt6ct/colors/matugen.conf";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("colortheme: cannot open qt6ct config: %s", qPrintable(path));
        return false;
    }

    // Manual INI parse — qt6ct uses "Group/Key=Value" format with comma-separated RGB
    QMap<QString, QMap<QString, QString>> data;
    QString currentGroup;

    QTextStream ts(&f);
    QString line;
    while (!ts.atEnd()) {
        line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';'))
            continue;
        if (line.startsWith('[') && line.endsWith(']')) {
            currentGroup = line.mid(1, line.length() - 2);
            continue;
        }
        int eq = line.indexOf('=');
        if (eq <= 0 || currentGroup.isEmpty())
            continue;
        QString key = line.left(eq).trimmed();
        QString val = line.mid(eq + 1).trimmed();
        data[currentGroup][key] = val;
    }
    f.close();

    m_surface          = getColor(data, "Colors:Window",       "BackgroundNormal",      QColor("#211F26"));
    m_onSurface        = getColor(data, "Colors:Window",       "ForegroundNormal",      QColor("#E6E1E5"));
    m_onSurfaceVariant = getColor(data, "Colors:Window",       "ForegroundInactive",    QColor("#CAC4D0"));
    m_primary          = getColor(data, "Colors:Button",       "DecorationFocus",       QColor("#D0BCFF"));
    if (!m_primary.isValid())
        m_primary      = getColor(data, "Colors:View",         "DecorationFocus",       QColor("#D0BCFF"));
    m_progressTrack    = getColor(data, "Colors:View",         "BackgroundAlternate",  QColor("#49454F"));

    qDebug("colortheme: loaded — surface=%s primary=%s",
           qPrintable(m_surface.name()), qPrintable(m_primary.name()));

    m_valid = !data.isEmpty();
    return true;
}


QColor ColorTheme::surface() const { return m_surface; }
QColor ColorTheme::onSurface() const { return m_onSurface; }
QColor ColorTheme::onSurfaceVariant() const { return m_onSurfaceVariant; }
QColor ColorTheme::primary() const { return m_primary; }
QColor ColorTheme::progressTrack() const { return m_progressTrack; }
