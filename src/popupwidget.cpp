#include "popupwidget.h"

#include <QPainter>
#include <QtMath>
#include <QPainterPath>
#include <QScreen>
#include <QGuiApplication>

static QColor batteryFillColor(int pct, bool charging, const QColor &defaultColor)
{
    if (pct <= 20) return QColor(0xFF, 0x6E, 0x6E);
    if (charging)   return QColor(0x79, 0xFF, 0xC1);
    return defaultColor;
}

static void drawBatteryIcon(QPainter &p, QRect r, int pct, bool charging,
                             const QColor &fill)
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
    p.setPen(Qt::NoPen);
    p.setBrush(batteryFillColor(pct, charging, fill));
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

static void drawSpinner(QPainter &p, const QRect &r, int angle, const QColor &color);

static void drawStylusIcon(QPainter &p, QRect r, const QColor &color)
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

PopupWidget::PopupWidget(QObject *parent)
    : QObject(parent)
    , m_layer(new WaylandLayerSurface(this))
    , m_animTimer(new QTimer(this))
    , m_dismissTimer(new QTimer(this))
    , m_spinnerTimer(new QTimer(this))
{
    updateLayoutCache();

    if (!m_layer->init(m_screenW, kHeight,
                       WaylandLayerSurface::AnchorTop
                       | WaylandLayerSurface::AnchorLeft
                       | WaylandLayerSurface::AnchorRight,
                       WaylandLayerSurface::Top)) {
        qWarning("stylus-popup: layer surface init failed");
    }

    if (!m_theme.loadFromQt6ct())
        qWarning("stylus-popup: using fallback theme colors");

    QScreen *scr = QGuiApplication::primaryScreen();
    double refreshRate = scr ? scr->refreshRate() : 60.0;
    m_animTimer->setInterval(static_cast<int>(1000.0 / refreshRate));
    m_animTimer->setTimerType(Qt::PreciseTimer);
    connect(m_animTimer, &QTimer::timeout, this, &PopupWidget::onAnimationTick);

    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(kDismissMs);
    connect(m_dismissTimer, &QTimer::timeout, this, &PopupWidget::slideOut);

    m_spinnerTimer->setInterval(kSpinnerMs);
    connect(m_spinnerTimer, &QTimer::timeout, this, &PopupWidget::onSpinnerTick);

    m_titleFont.setPixelSize(14);
    m_titleFont.setWeight(QFont::Medium);

    m_subFont = m_titleFont;
    m_subFont.setPixelSize(12);
    m_subFont.setWeight(QFont::Normal);

    m_limitFont = m_subFont;
    m_limitFont.setPixelSize(9);
}

void PopupWidget::updateStatusText()
{
    if (!canShowFinal()) {
        m_statusText = QStringLiteral("Connecting…");
        m_limitText.clear();
        return;
    }

    m_statusText = m_state.charging
        ? QStringLiteral("Charging · %1 %").arg(m_state.capacity)
        : QStringLiteral("Connected · %1 %").arg(m_state.capacity);
    m_limitText = QStringLiteral("%1%").arg(m_state.limit);
}

bool PopupWidget::canShowFinal() const
{
    return m_state.attached
        && m_state.phase == StylusPhase::Complete
        && m_btConnected;
}

int PopupWidget::targetHeightForState() const
{
    return canShowFinal() ? kHeight : kWaitingHeight;
}

void PopupWidget::showState(const StylusState &state)
{
    const bool wasFinal = canShowFinal();

    if (state != m_state) {
        m_state = state;
        m_dirty = true;
    }

    m_dismissTimer->stop();

    if (!state.attached) {
        m_btConnected = false;
        m_spinnerTimer->stop();
        if (m_shown)
            slideOut();
        return;
    }

    updateStatusText();

    if (state.phase == StylusPhase::Attaching)
        m_btConnected = false;

    if (!m_shown) {
        slideIn(targetHeightForState());
        if (!canShowFinal())
            m_spinnerTimer->start();
        return;
    }

    if (canShowFinal() && !wasFinal) {
        m_spinnerTimer->stop();
        transitionToFinal();
        return;
    }

    if (m_dirty)
        renderFrame();
    if (canShowFinal())
        m_dismissTimer->start();
}

void PopupWidget::onBtConnected()
{
    if (!m_state.attached)
        return;

    m_btConnected = true;
    if (canShowFinal())
        transitionToFinal();
}

void PopupWidget::onBtConnectionFailed(const QString &error)
{
    qWarning("PopupWidget: Bluetooth connection failed: %s", qPrintable(error));
}

void PopupWidget::transitionToFinal()
{
    updateStatusText();
    m_dirty = true;

    if (m_layer->visibleHeight() < kHeight) {
        startAnimation(m_layer->visibleHeight(), kHeight, QEasingCurve::OutExpo);
    } else {
        renderFrame();
    }
}

void PopupWidget::slideIn(int targetHeight)
{
    startAnimation(0, targetHeight, QEasingCurve::OutExpo);
}

void PopupWidget::slideOut()
{
    m_spinnerTimer->stop();
    startAnimation(m_layer->visibleHeight(), 0, QEasingCurve::InExpo);
}

void PopupWidget::startAnimation(int fromH, int toH, const QEasingCurve &curve)
{
    m_animStart  = fromH;
    m_animEnd    = toH;
    m_animCurve  = curve;
    m_elapsed.start();
    m_animTimer->start();
    m_shown      = (toH > 0);
}

void PopupWidget::onAnimationTick()
{
    double t = qBound(0.0, static_cast<double>(m_elapsed.elapsed()) / kAnimMs, 1.0);
    double progress = m_animCurve.valueForProgress(t);

    int h = static_cast<int>(m_animStart + (m_animEnd - m_animStart) * progress);

    if (h != m_layer->visibleHeight()) {
        m_layer->setVisibleHeight(h);
        renderFrame();
    }

    if (t >= 1.0) {
        m_animTimer->stop();
        if (m_animEnd == 0) {
            m_layer->hide();
        } else if (m_animEnd == kHeight) {
            m_dismissTimer->start();
        }
    }
}

void PopupWidget::onSpinnerTick()
{
    m_spinnerAngle = (m_spinnerAngle + 10) % 360;
    if (m_shown && !canShowFinal()) {
        m_dirty = true;
        renderFrame();
    }
}

void PopupWidget::updateLayoutCache()
{
    QScreen *scr = QGuiApplication::primaryScreen();
    m_screenW = scr ? scr->geometry().width() : 1080;

    const int cardW = qMin(m_screenW, kWidth);
    const int cardX = (m_screenW - cardW) / 2;
    m_cardRect = QRect(cardX, 4, cardW, kHeight - 8);

    const int waitingTextW = QFontMetrics(m_subFont).horizontalAdvance(QStringLiteral("Connecting…"));
    const int waitingCardW = qMin(waitingTextW + kSpinnerSize + 3 * kPad, m_screenW - 8);
    const int waitingCardX = (m_screenW - waitingCardW) / 2;
    m_waitingCardRect = QRect(waitingCardX, 4, waitingCardW, kWaitingHeight - 8);

    m_cardPath = QPainterPath();
    m_cardPath.addRoundedRect(m_cardRect, 16, 16);
    m_waitingCardPath = QPainterPath();
    m_waitingCardPath.addRoundedRect(m_waitingCardRect, 16, 16);
    m_borderPen = QPen(QColor(255, 255, 255, 30), 1);

    m_iconX = m_cardRect.x() + kPad;
    m_iconY = m_cardRect.y() + (m_cardRect.height() - kIconH) / 2;

    m_textX = m_iconX + kIconW + kPad;
    m_titleY = m_cardRect.y() + kPad + 14;
    m_subY   = m_cardRect.y() + kPad + 34;

    m_barY = m_cardRect.bottom() - kPad - kBarH;
    m_barW = m_cardRect.right() - kPad - m_textX;

    m_batX = m_cardRect.right() - kPad - kBatW;
    m_batY = m_cardRect.y() + (m_cardRect.height() - kBatH) / 2;
}

/* Render the MD3 card to a QImage, then push to the layer surface */

void PopupWidget::renderFrame()
{
    if (!m_layer->isReady() || m_layer->visibleHeight() <= 0) return;

    const int scale = m_layer->scale();

    if (m_imageBuffer.width() != m_screenW * scale || m_imageBuffer.height() != kHeight * scale) {
        m_imageBuffer = QImage(m_screenW * scale, kHeight * scale, QImage::Format_ARGB32_Premultiplied);
        m_dirty = true;
    }

    if (m_dirty) {
        m_imageBuffer.fill(Qt::transparent);

        QPainter p(&m_imageBuffer);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(scale, scale);

        const QColor &surface       = m_theme.surface();
        const QColor &onSurface     = m_theme.onSurface();
        const QColor &onSurfaceVar  = m_theme.onSurfaceVariant();
        const QColor &primary       = m_theme.primary();
        const QColor &progressTrack = m_theme.progressTrack();

        /* ── Card background ── */
        const QPainterPath &cardPath = canShowFinal() ? m_cardPath : m_waitingCardPath;
        p.fillPath(cardPath, surface);
        p.setPen(m_borderPen);
        p.drawPath(cardPath);

        if (canShowFinal()) {
            /* ── Icon ── */
            drawStylusIcon(p, QRect(m_iconX, m_iconY, kIconW, kIconH), primary);

            /* ── Text ── */
            p.setFont(m_titleFont);
            p.setPen(onSurface);
            p.drawText(m_textX, m_titleY, QStringLiteral("Xiaomi Stylus Pen 2"));

            p.setFont(m_subFont);
            p.setPen(onSurfaceVar);
            p.drawText(m_textX, m_subY, m_statusText);

            /* ── Battery progress bar ── */
            p.setPen(Qt::NoPen);
            p.setBrush(progressTrack);
            p.drawRoundedRect(m_textX, m_barY, m_barW, kBarH, kBarH/2, kBarH/2);

            int fillW = qMax(kBarH, int(m_barW * m_state.capacity / 100.0));
            p.setBrush(batteryFillColor(m_state.capacity, m_state.charging, primary));
            p.drawRoundedRect(m_textX, m_barY, fillW, kBarH, kBarH/2, kBarH/2);

            /* Limit marker */
            if (m_state.limit > 0 && m_state.limit <= 100) {
                int lx = m_textX + int(m_barW * m_state.limit / 100.0);
                p.setBrush(onSurfaceVar);
                p.drawEllipse(QPoint(lx, m_barY + kBarH/2), 4, 4);
                p.setFont(m_limitFont);
                p.setPen(onSurfaceVar);
                p.drawText(lx - 10, m_barY - 4, m_limitText);
            }

            /* ── Battery icon (top-right corner of card) ── */
            drawBatteryIcon(p, QRect(m_batX, m_batY, kBatW, kBatH),
                           m_state.capacity, m_state.charging,
                           primary);
        } else {
            /* ── Waiting spinner + text ── */
            const QRect &wr = m_waitingCardRect;
            const int spinnerX = wr.x() + kPad;
            const int spinnerY = wr.y() + (wr.height() - kSpinnerSize) / 2;
            drawSpinner(p, QRect(spinnerX, spinnerY, kSpinnerSize, kSpinnerSize),
                        m_spinnerAngle, primary);

            p.setFont(m_subFont);
            p.setPen(onSurfaceVar);
            const QFontMetrics fm(m_subFont);
            const QRect textBounds = fm.tightBoundingRect(m_statusText);
            const int textX = spinnerX + kSpinnerSize + kPad - textBounds.left();
            const int textH = fm.ascent() + fm.descent();
            const int textY = wr.y() + (wr.height() - textH) / 2 + fm.ascent();
            p.drawText(textX, textY, m_statusText);
        }

        m_dirty = false;
    }

    m_layer->updateImage(m_imageBuffer);
    m_layer->commitFrame();
}

static void drawSpinner(QPainter &p, const QRect &r, int angle, const QColor &color)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    const QPoint center = r.center();
    const int trackRadius = r.width() / 2 - 3;
    const int ballRadius = trackRadius / 2;
    const int ballOrbitRadius = trackRadius - ballRadius;

    /* Static circular track */
    QPen trackPen(color.darker(180), 3.0);
    trackPen.setCapStyle(Qt::RoundCap);
    p.setPen(trackPen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(center, trackRadius, trackRadius);

    /* Solid ball moving along the inner arc of the track */
    const qreal a = qDegreesToRadians(static_cast<qreal>(angle));
    const QPoint ballPos(center.x() + static_cast<int>(ballOrbitRadius * qCos(a)),
                         center.y() + static_cast<int>(ballOrbitRadius * qSin(a)));
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(ballPos, ballRadius, ballRadius);

    p.restore();
}
