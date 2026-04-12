#include "popupwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QGuiApplication>
#include <QEasingCurve>
#include <QDateTime>

/* Drawing helpers */

static void drawBatteryIcon(QPainter &p, QRect r, int pct, bool charging,
                             QColor fill, QColor /*track*/)
{
    const int bw = r.width() - 4, bh = r.height() - 8;
    const int bx = r.x() + 2,    by = r.y() + 4;
    const int nub = 3;

    p.setPen(QPen(fill, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(bx, by + nub, bw, bh, 3, 3);
    p.drawRoundedRect(bx + bw / 4, by, bw / 2, nub + 2, 1, 1);

    int fillH = qMax(2, int((bh - 4) * pct / 100.0));
    int fillY = by + nub + 2 + (bh - 4) - fillH;
    QColor fc = (pct <= 20) ? QColor(0xFF, 0x6E, 0x6E)
              : charging    ? QColor(0x79, 0xFF, 0xC1)
              :               fill;
    p.setPen(Qt::NoPen);
    p.setBrush(fc);
    p.drawRoundedRect(bx + 2, fillY, bw - 4, fillH, 2, 2);

    if (charging) {
        const int cx = bx + bw / 2, cy = by + nub + bh / 2;
        QPainterPath bolt;
        bolt.moveTo(cx + 2, cy - 5); bolt.lineTo(cx - 2, cy);
        bolt.lineTo(cx + 1, cy);     bolt.lineTo(cx - 2, cy + 5);
        bolt.lineTo(cx + 2, cy);     bolt.lineTo(cx - 1, cy);
        bolt.closeSubpath();
        p.setBrush(QColor(0x1C, 0x1B, 0x1F));
        p.drawPath(bolt);
    }
}

static void drawStylusIcon(QPainter &p, QRect r, QColor color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const int cx = r.center().x(), cy = r.center().y();
    QPainterPath barrel;
    barrel.moveTo(cx - 4, cy - 14); barrel.lineTo(cx + 4, cy - 14);
    barrel.lineTo(cx + 4, cy + 4);  barrel.lineTo(cx, cy + 14);
    barrel.lineTo(cx - 4, cy + 4);  barrel.closeSubpath();
    p.setPen(QPen(color, 1.2));
    p.setBrush(color.darker(180));
    p.drawPath(barrel);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRect(cx - 4, cy - 2, 8, 4);
    p.setBrush(color.lighter(130));
    p.drawRoundedRect(cx - 4, cy - 14, 8, 5, 2, 2);
    p.restore();
}

/* PopupWidget */

PopupWidget::PopupWidget(QObject *parent)
    : QObject(parent)
    , m_layer(new WaylandLayerSurface(this))
    , m_animTimer(new QTimer(this))
    , m_dismissTimer(new QTimer(this))
{
    QScreen *scr = QGuiApplication::primaryScreen();
    int screenW  = scr ? scr->geometry().width() : 1080;

    if (!m_layer->init(screenW, kHeight,
                       WaylandLayerSurface::AnchorTop
                       | WaylandLayerSurface::AnchorLeft
                       | WaylandLayerSurface::AnchorRight,
                       WaylandLayerSurface::Top)) {
        qWarning("stylus-popup: layer surface init failed");
    }

    m_theme.loadFromQt6ct();

    /* Animation driven by screen refresh rate via QTimer */
    double refreshRate = scr ? scr->refreshRate() : 60.0;
    m_animTimer->setInterval(static_cast<int>(1000.0 / refreshRate));
    m_animTimer->setTimerType(Qt::PreciseTimer);
    connect(m_animTimer, &QTimer::timeout, this, &PopupWidget::onAnimationTick);

    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(kDismissMs);
    connect(m_dismissTimer, &QTimer::timeout, this, &PopupWidget::slideOut);
}

void PopupWidget::showState(const StylusState &state)
{
    if (state == m_state && m_shown) {
        m_dismissTimer->stop();
        m_dismissTimer->start();
        return;
    }

    m_state = state;
    m_dismissTimer->stop();

    if (m_shown) {
        renderFrame();
        m_dismissTimer->start();
        return;
    }

    slideIn();
}

void PopupWidget::slideIn()
{
    m_animTimer->stop();
    m_animStart = 0;
    m_animEnd = m_layer->fullHeight();
    m_animReversed = false;
    m_animCurve = QEasingCurve::OutExpo;
    m_animDuration = kAnimMs;
    m_animBegin = QDateTime::currentMSecsSinceEpoch();
    m_animTimer->start();
    m_shown = true;
    m_dismissTimer->start();
}

void PopupWidget::slideOut()
{
    m_animTimer->stop();
    m_animStart = m_layer->visibleHeight();
    m_animEnd = 0;
    m_animReversed = true;
    m_animCurve = QEasingCurve::InExpo;
    m_animDuration = kAnimMs;
    m_animBegin = QDateTime::currentMSecsSinceEpoch();
    m_animTimer->start();
    m_shown = false;
}

void PopupWidget::onAnimationTick()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    double t = qBound(0.0, static_cast<double>(now - m_animBegin) / m_animDuration, 1.0);
    double progress = m_animCurve.valueForProgress(t);

    int h = static_cast<int>(m_animStart + (m_animEnd - m_animStart) * progress);
    m_layer->setVisibleHeight(h);
    renderFrame();

    if (t >= 1.0) {
        m_animTimer->stop();
        if (m_animReversed) {
            m_layer->hide();
        }
    }
}

/* Render the MD3 card to a QImage, then push to the layer surface */

void PopupWidget::renderFrame()
{
    if (!m_layer->isReady()) return;

    QScreen *scr = QGuiApplication::primaryScreen();
    int screenW  = scr ? scr->geometry().width() : 1080;
    const int scale = m_layer->scale();

    /* Reuse image buffer to avoid per-frame allocation churn */
    static QImage img;
    if (img.width() != screenW * scale || img.height() != kHeight * scale) {
        img = QImage(screenW * scale, kHeight * scale, QImage::Format_ARGB32_Premultiplied);
    }
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);

    const QColor surface       = m_theme.isValid() ? m_theme.surface()       : QColor(kSurface);
    const QColor onSurface     = m_theme.isValid() ? m_theme.onSurface()     : QColor(kOnSurface);
    const QColor onSurfaceVar  = m_theme.isValid() ? m_theme.onSurfaceVariant() : QColor(kOnSurfaceVar);
    const QColor primary       = m_theme.isValid() ? m_theme.primary()       : QColor(kPrimary);
    const QColor progressTrack = m_theme.isValid() ? m_theme.progressTrack() : QColor(kProgressTrack);

    /* Card rect: centred horizontally, full height */
    const int pad   = 16;
    const int cardW = qMin(screenW, kWidth);
    const int cardX = (screenW - cardW) / 2;
    const QRect card(cardX, 4, cardW, kHeight - 8);

    /* ── Card background ── */
    QPainterPath bg;
    bg.addRoundedRect(card, 16, 16);
    p.setBrush(surface);
    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.drawPath(bg);

    /* ── Icon ── */
    const int iconW = 32, iconH = 52;
    const int iconX = card.x() + pad;
    const int iconY = card.y() + (card.height() - iconH) / 2;
    drawStylusIcon(p, QRect(iconX, iconY, iconW, iconH), primary);

    /* ── Text ── */
    const int textX = iconX + iconW + pad;

    QFont titleFont = p.font();
    titleFont.setPixelSize(15);
    titleFont.setWeight(QFont::Medium);
    p.setFont(titleFont);
    p.setPen(onSurface);
    p.drawText(textX, card.y() + pad + 14, "Xiaomi Stylus Pen 2");

    QFont subFont = titleFont;
    subFont.setPixelSize(13);
    subFont.setWeight(QFont::Normal);
    p.setFont(subFont);
    p.setPen(onSurfaceVar);
    QString status = m_state.charging
        ? QString("Charging · %1 %").arg(m_state.capacity)
        : QString("Connected · %1 %").arg(m_state.capacity);
    p.drawText(textX, card.y() + pad + 34, status);

    /* ── Battery progress bar ── */
    const int barH  = 6;
    const int barY  = card.bottom() - pad - barH;
    const int barW  = card.right() - pad - textX;

    p.setPen(Qt::NoPen);
    p.setBrush(progressTrack);
    p.drawRoundedRect(textX, barY, barW, barH, barH/2, barH/2);

    int fillW = qMax(barH, int(barW * m_state.capacity / 100.0));
    QColor fc = (m_state.capacity <= 20) ? QColor(0xFF, 0x6E, 0x6E)
              : m_state.charging         ? QColor(0x79, 0xFF, 0xC1)
              :                            primary;
    p.setBrush(fc);
    p.drawRoundedRect(textX, barY, fillW, barH, barH/2, barH/2);

    /* Limit marker */
    if (m_state.limit > 0 && m_state.limit <= 100) {
        int lx = textX + int(barW * m_state.limit / 100.0);
        p.setBrush(onSurfaceVar);
        p.drawEllipse(QPoint(lx, barY + barH/2), 4, 4);
        QFont lf = subFont; lf.setPixelSize(10);
        p.setFont(lf);
        p.setPen(onSurfaceVar);
        p.drawText(lx - 10, barY - 4, QString("%1%").arg(m_state.limit));
    }

    /* ── Battery icon (top-right corner of card) ── */
    const int batW = 22, batH = 38;
    drawBatteryIcon(p, QRect(card.right() - pad - batW,
                             card.y() + (card.height() - batH)/2,
                             batW, batH),
                    m_state.capacity, m_state.charging,
                    primary, progressTrack);

    p.end();
    m_layer->renderImage(img);
}
