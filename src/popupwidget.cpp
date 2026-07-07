#include "popupwidget.h"

#include <QPainter>
#include <QtMath>
#include <QPainterPath>
#include <QScreen>
#include <QGuiApplication>

static void drawSpinner(QPainter &p, const QRect &r, int angle, const QColor &color);

static QString stylusNameForMac(const QString &mac, bool macValid)
{
    // Xiaomi's second-generation stylus reports a fixed MAC address.
    // All other pens from the same era use the same hardware/firmware and
    // are treated as the first generation.
    static const QString kGen2Mac = QStringLiteral("E6:FB:D0:E1:5A:04");
    if (macValid && mac.compare(kGen2Mac, Qt::CaseInsensitive) == 0)
        return QStringLiteral("Xiaomi Stylus Pen 2");
    return QStringLiteral("Xiaomi Stylus Pen 1");
}

static void drawBatteryGlyph(QPainter &p, const QRect &r, const ColorTheme &theme,
                              int pct, bool charging, qreal pulsePhase)
{
    const QColor &primary       = theme.primary();
    const QColor &track         = theme.progressTrack();
    const QColor &onSurface     = theme.onSurface();
    const QColor &chargingColor = theme.charging();
    const QColor &lowBattery    = theme.lowBattery();

    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    const QPoint center = r.center();
    const int outerRadius = r.width() / 2 - 2;
    const int ringRadius = outerRadius - 4;
    const qreal penW = 5.0;

    /* Outer glow when charging */
    if (charging) {
        const qreal glow = 0.35 + 0.25 * qSin(pulsePhase * M_PI * 2);
        QColor glowColor = chargingColor;
        glowColor.setAlphaF(glow);
        p.setPen(Qt::NoPen);
        p.setBrush(glowColor);
        p.drawEllipse(center, outerRadius + 4, outerRadius + 4);
    }

    /* Track ring */
    QPen trackPen(track, penW);
    trackPen.setCapStyle(Qt::RoundCap);
    p.setPen(trackPen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(center, ringRadius, ringRadius);

    /* Battery level arc */
    const QColor arcColor = charging ? chargingColor : (pct <= 20 ? lowBattery : primary);
    QPen arcPen(arcColor, penW);
    arcPen.setCapStyle(Qt::RoundCap);
    p.setPen(arcPen);
    const int span = -qBound(0, pct, 100) * 5760 / 100; /* 5760 = 360° * 16 (QPainter arc units) */
    p.drawArc(center.x() - ringRadius, center.y() - ringRadius,
              ringRadius * 2, ringRadius * 2,
              90 * 16, span);

    /* Percentage text */
    QFont f = p.font();
    f.setPixelSize(qMax(10, r.width() / 4));
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(onSurface);
    const QString text = QStringLiteral("%1%").arg(pct);
    const QFontMetrics fm(f);
    const QRect textBounds = fm.tightBoundingRect(text);
    const int baseline = center.y() + (fm.ascent() - fm.descent()) / 2;
    p.drawText(center.x() - textBounds.width() / 2 - textBounds.left(),
               baseline,
               text);

    p.restore();
}

static void drawCapsuleBackground(QPainter &p, const QRect &rect, qreal radius,
                                   const QColor &surface, const QColor &border)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);

    /* Soft drop shadow */
    QPainterPath shadowPath = path;
    shadowPath.translate(0, 4);
    QColor shadowColor(0, 0, 0, 35);
    p.fillPath(shadowPath, shadowColor);

    /* Capsule fill */
    p.fillPath(path, surface);

    /* Border */
    QPen borderPen(border, 2.0);
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

PopupWidget::PopupWidget(QObject *parent)
    : QObject(parent)
    , m_layer(new WaylandLayerSurface(this))
    , m_animTimer(new QTimer(this))
    , m_dismissTimer(new QTimer(this))
    , m_spinnerTimer(new QTimer(this))
{
    updateLayoutCache();

    if (!m_layer->init(m_screenW, kSurfaceHeight,
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

    m_subFont.setPixelSize(12);
    m_subFont.setWeight(QFont::Normal);
}

bool PopupWidget::canShowFinal() const
{
    return m_state.attached
        && m_state.phase == StylusPhase::Complete
        && m_btConnected;
}

int PopupWidget::targetHeightForState() const
{
    return canShowFinal()
        ? kSurfaceHeight
        : kCapsuleTopMargin + kWaitingHeight + kCapsuleTopMargin;
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

    if (state.phase == StylusPhase::Attaching)
        m_btConnected = false;

    if (!m_shown) {
        slideIn(targetHeightForState());
        return;
    }

    if (canShowFinal() && !wasFinal) {
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
    m_dirty = true;

    if (m_layer->visibleHeight() < kSurfaceHeight) {
        m_morphing = true;
        m_morphProgress = 0.0;
        startAnimation(m_layer->visibleHeight(), kSurfaceHeight, QEasingCurve::OutCubic);
    } else {
        m_morphing = false;
        m_morphProgress = 1.0;
        renderFrame();
        if (canShowFinal() && !m_dismissTimer->isActive())
            m_dismissTimer->start();
    }
}

void PopupWidget::slideIn(int targetHeight)
{
    startAnimation(0, targetHeight, QEasingCurve::OutBack);
    m_spinnerTimer->start();
}

void PopupWidget::slideOut()
{
    m_spinnerTimer->stop();
    startAnimation(m_layer->visibleHeight(), 0, QEasingCurve::InBack);
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

    if (m_morphing)
        m_morphProgress = t;

    int h = qBound(0, static_cast<int>(m_animStart + (m_animEnd - m_animStart) * progress), kSurfaceHeight);

    if (h != m_layer->visibleHeight() || m_morphing) {
        m_dirty = true;
        m_layer->setVisibleHeight(h);
        renderFrame();
    }

    if (t >= 1.0) {
        m_animTimer->stop();
        if (m_animEnd == 0) {
            m_layer->hide();
        } else if (m_animEnd == kSurfaceHeight && canShowFinal()) {
            m_morphing = false;
            m_morphProgress = 1.0;
            m_dismissTimer->start();
        }
    }
}

void PopupWidget::onSpinnerTick()
{
    m_spinnerAngle = (m_spinnerAngle + 10) % 360;
    m_pulsePhase += 0.04;
    if (m_pulsePhase > 1.0)
        m_pulsePhase -= 1.0;

    if (m_shown && (!canShowFinal() || m_state.charging)) {
        m_dirty = true;
        renderFrame();
    }
}

void PopupWidget::updateLayoutCache()
{
    QScreen *scr = QGuiApplication::primaryScreen();
    m_screenW = scr ? scr->geometry().width() : 1080;

    const int capsuleW = qMin(kCapsuleWidth, m_screenW - 16);
    const int capsuleX = (m_screenW - capsuleW) / 2;
    m_capsuleRect = QRect(capsuleX, kCapsuleTopMargin, capsuleW, kCapsuleHeight);

    const int chipX = (m_screenW - kWaitingWidth) / 2;
    m_waitingChipRect = QRect(chipX, kCapsuleTopMargin, kWaitingWidth, kWaitingHeight);

    m_glyphRect = QRect(m_capsuleRect.x() + kSpinnerTextGap,
                        m_capsuleRect.y() + (m_capsuleRect.height() - kBatteryGlyphSize) / 2,
                        kBatteryGlyphSize, kBatteryGlyphSize);

    const int limitW = 38;
    const int limitH = 34;
    m_limitRect = QRect(m_capsuleRect.right() - kSpinnerTextGap - limitW,
                        m_capsuleRect.y() + (m_capsuleRect.height() - limitH) / 2,
                        limitW, limitH);

    m_textRect = QRect(m_glyphRect.right() + kSpinnerTextGap,
                       m_capsuleRect.y(),
                       m_limitRect.left() - m_glyphRect.right() - 2 * kSpinnerTextGap,
                       m_capsuleRect.height());
}

void PopupWidget::drawFinalContent(QPainter &p)
{
    drawBatteryGlyph(p, m_glyphRect, m_theme,
                     m_state.capacity, m_state.charging, m_pulsePhase);

    p.setFont(m_titleFont);
    p.setPen(m_theme.onSurface());
    const QFontMetrics fmTitle(m_titleFont);
    const QString titleText = stylusNameForMac(m_state.macAddress, m_state.macValid);
    const QRect titleTb = fmTitle.tightBoundingRect(titleText);
    const int titleX = m_textRect.center().x() - titleTb.width() / 2 - titleTb.left();
    const int titleY = m_textRect.center().y() + (fmTitle.ascent() - fmTitle.descent()) / 2;
    p.drawText(titleX, titleY, titleText);

    if (m_state.limit > 0 && m_state.limit <= 100)
        drawLimitBadge(p);
}

void PopupWidget::drawLimitBadge(QPainter &p)
{
    const QColor &surface      = m_theme.surface();
    const QColor &primary      = m_theme.primary();
    const QColor &onSurfaceVar = m_theme.onSurfaceVariant();

    QColor fill = surface.lighter(160);
    fill.setAlphaF(0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(m_limitRect, 10, 10);

    QColor border = surface.lighter(240);
    border.setAlphaF(0.85);
    p.setPen(QPen(border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(m_limitRect, 10, 10);

    QFont labelFont(m_subFont);
    labelFont.setPixelSize(9);
    labelFont.setWeight(QFont::Medium);

    QFont valueFont(m_subFont);
    valueFont.setWeight(QFont::DemiBold);

    const QString valueText = QStringLiteral("%1%").arg(m_state.limit);
    const QFontMetrics fmLabel(labelFont);
    const QFontMetrics fmValue(valueFont);
    const QRect labelTb = fmLabel.tightBoundingRect(QStringLiteral("LIMIT"));
    const QRect valueTb = fmValue.tightBoundingRect(valueText);

    const int cx = m_limitRect.center().x();
    const int totalH = labelTb.height() + 2 + valueTb.height();
    const int topY = m_limitRect.center().y() - totalH / 2;

    p.setFont(labelFont);
    p.setPen(onSurfaceVar);
    p.drawText(cx - labelTb.width() / 2 - labelTb.left(),
               topY + labelTb.height() - labelTb.bottom(),
               QStringLiteral("LIMIT"));

    p.setFont(valueFont);
    p.setPen(primary);
    p.drawText(cx - valueTb.width() / 2 - valueTb.left(),
               topY + labelTb.height() + 2 + valueTb.height() - valueTb.bottom(),
               valueText);
}

void PopupWidget::renderFrame()
{
    if (!m_layer->isReady() || m_layer->visibleHeight() <= 0) return;

    const int scale = m_layer->scale();

    if (m_imageBuffer.width() != m_screenW * scale || m_imageBuffer.height() != kSurfaceHeight * scale) {
        m_imageBuffer = QImage(m_screenW * scale, kSurfaceHeight * scale, QImage::Format_ARGB32_Premultiplied);
        m_dirty = true;
    }

    if (m_dirty) {
        m_imageBuffer.fill(Qt::transparent);

        QPainter p(&m_imageBuffer);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(scale, scale);

        const QColor &surface       = m_theme.surface();
        const QColor &onSurfaceVar  = m_theme.onSurfaceVariant();
        const QColor &primary       = m_theme.primary();

        QColor border = surface.lighter(220);
        border.setAlphaF(0.75);

        if (canShowFinal() && m_morphing) {
            /* ── Morphing transition: chip → capsule ── */
            qreal t = static_cast<qreal>(m_morphProgress);
            QRect curRect(
                m_waitingChipRect.x() + static_cast<int>((m_capsuleRect.x() - m_waitingChipRect.x()) * t),
                m_waitingChipRect.y() + static_cast<int>((m_capsuleRect.y() - m_waitingChipRect.y()) * t),
                m_waitingChipRect.width() + static_cast<int>((m_capsuleRect.width() - m_waitingChipRect.width()) * t),
                m_waitingChipRect.height() + static_cast<int>((m_capsuleRect.height() - m_waitingChipRect.height()) * t));
            const int startR = kWaitingHeight / 2;
            const int endR = kCapsuleHeight / 2;
            const int curR = startR + static_cast<int>((endR - startR) * t);

            QPainterPath curPath;
            curPath.addRoundedRect(curRect, curR, curR);
            drawCapsuleBackground(p, curRect, curR, surface, border);
            p.setClipPath(curPath);

            qreal waitingAlpha = qBound(0.0, 1.0 - t / 0.35, 1.0);
            qreal finalAlpha   = qBound(0.0, (t - 0.35) / 0.45, 1.0);

            if (waitingAlpha > 0.0) {
                p.save();
                p.setOpacity(waitingAlpha);
                                const int spinnerX = curRect.x() + kSpinnerTextGap;
                const int spinnerY = curRect.y() + (curRect.height() - kSpinnerSize) / 2;
                drawSpinner(p, QRect(spinnerX, spinnerY, kSpinnerSize, kSpinnerSize),
                            m_spinnerAngle, primary);
                p.restore();
            }

            if (finalAlpha > 0.0) {
                p.save();
                p.setOpacity(finalAlpha);
                drawFinalContent(p);
                p.restore();
            }
            p.setClipping(false);
        } else if (canShowFinal()) {
            /* ── Floating capsule background ── */
            drawCapsuleBackground(p, m_capsuleRect, kCapsuleHeight / 2.0, surface, border);

            drawFinalContent(p);
        } else {
            /* ── Waiting chip ── */
            drawCapsuleBackground(p, m_waitingChipRect, kWaitingHeight / 2.0, surface, border);

            const int spinnerX = m_waitingChipRect.x() + kSpinnerTextGap;
            const int spinnerY = m_waitingChipRect.y() + (m_waitingChipRect.height() - kSpinnerSize) / 2;
            drawSpinner(p, QRect(spinnerX, spinnerY, kSpinnerSize, kSpinnerSize),
                        m_spinnerAngle, primary);

            p.setFont(m_subFont);
            p.setPen(onSurfaceVar);
            const QFontMetrics fm(m_subFont);
            const QRect textBounds = fm.tightBoundingRect(QStringLiteral("Connecting…"));
            const int textX = spinnerX + kSpinnerSize + kSpinnerTextGap - textBounds.left();
            const int textY = m_waitingChipRect.y() + (m_waitingChipRect.height() + fm.ascent() - fm.descent()) / 2;
            p.drawText(textX, textY, QStringLiteral("Connecting…"));
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
    const int radius = r.width() / 2 - 3;
    const qreal penW = 2.5;

    /* MD3-style flat track ring */
    QColor trackColor = color;
    trackColor.setAlphaF(0.12);
    QPen trackPen(trackColor, penW);
    trackPen.setCapStyle(Qt::RoundCap);
    p.setPen(trackPen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(center, radius, radius);

    /* MD3 indeterminate arc: fixed sweep, smooth rotation */
    const int span = 90 * 16;

    const int startAngle = (90 - angle) * 16;

    QPen arcPen(color, penW);
    arcPen.setCapStyle(Qt::RoundCap);
    p.setPen(arcPen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(center.x() - radius, center.y() - radius,
              radius * 2, radius * 2,
              startAngle, -span);

    p.restore();
}
