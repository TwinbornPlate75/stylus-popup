#include "popupwidget.h"

#include "islandstrings.h"

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QtMath>

#include <iterator>

namespace {

/* Spring presets: (stiffness, damping). A damping ratio of
 * c / (2 * sqrt(k)) below 1 overshoots a little, 1 arrives without bounce. */
constexpr qreal kMorphK  = 380, kMorphC  = 26;   /* ζ ≈ 0.67: growing shapes */
constexpr qreal kSettleK = 420, kSettleC = 41;   /* ζ ≈ 1:    shrinking away */
constexpr qreal kFadeK   = 600, kFadeC   = 49;   /* ζ ≈ 1:    content fades */
constexpr qreal kShakeK  = 900, kShakeC  = 12;   /* ζ ≈ 0.2:  error shake   */
constexpr qreal kPulseK  = 500, kPulseC  = 22;   /* ζ ≈ 0.5:  charging pulse */
constexpr qreal kFillK   = 120, kFillC   = 22;   /* ζ ≈ 1:    battery ring  */

/* Initial velocities that give the feedback motions their size: about ±8 px
 * for the shake and about +4 % for the pulse. */
constexpr qreal kShakeKick = 260.0;
constexpr qreal kPulseKick = 1.6;

/* Content reveals once the shape is this far along its morph, and the dot
 * starts to vanish once a collapsing shape is this far along. */
constexpr qreal kRevealAt = 0.6;
constexpr qreal kVanishAt = 0.85;

/* A compact pill is a 20 px leading box - the spinner or the error badge -
 * then a 10 px gap and the label, with a 12 px pad at the end. */
constexpr qreal kCompactIcon = 20.0;
constexpr qreal kCompactGap  = 10.0;
constexpr qreal kCompactPad  = 12.0;

/** The two boxes a compact pill is laid out from. */
struct CompactRow {
    QRectF leading;  /* the spinner or the error badge */
    QRectF text;     /* the label column; the label elides into it */
};

/** Both compact forms are laid out the same way, so the geometry is defined
 *  once: the leading box sits `inset` in from the rounded end, which puts its
 *  centre on the end's centre, and the label takes what is left. */
CompactRow compactRowFor(const QRectF &r)
{
    const qreal inset = (r.height() - kCompactIcon) / 2;

    CompactRow row;
    row.leading = QRectF(r.left() + inset, r.center().y() - kCompactIcon / 2,
                         kCompactIcon, kCompactIcon);
    row.text = QRectF(row.leading.right() + kCompactGap, r.top(),
                      r.right() - row.leading.right() - kCompactGap - kCompactPad,
                      r.height());
    return row;
}

QColor withAlpha(QColor c, qreal alpha)
{
    c.setAlphaF(alpha);
    return c;
}

qreal easeInOutCubic(qreal x)
{
    return x < 0.5 ? 4 * x * x * x : 1 - qPow(-2 * x + 2, 3) / 2;
}

/** A clockwise arc starting at `startDeg` (0 = 12 o'clock), as a path. */
QPainterPath clockwiseArc(const QRectF &r, qreal startDeg, qreal spanDeg)
{
    QPainterPath path;
    path.arcMoveTo(r, 90 - startDeg);
    path.arcTo(r, 90 - startDeg, -spanDeg);
    return path;
}

void drawPenGlyph(QPainter &p, const QPointF &centre, qreal size, const QColor &color)
{
    p.save();
    p.translate(centre);
    p.rotate(45);
    p.setPen(Qt::NoPen);
    p.setBrush(color);

    const qreal w = size * 0.26;
    p.drawRoundedRect(QRectF(-w / 2, -size * 0.5, w, size * 0.7), w / 2, w / 2);

    QPainterPath tip;
    tip.moveTo(-w / 2, size * 0.26);
    tip.lineTo(w / 2, size * 0.26);
    tip.lineTo(0, size * 0.5);
    tip.closeSubpath();
    p.drawPath(tip);
    p.restore();
}

void drawBolt(QPainter &p, const QRectF &box, const QColor &color)
{
    static const QPointF kBolt[] = {
        {0.58, 0.04}, {0.18, 0.56}, {0.46, 0.56},
        {0.40, 0.96}, {0.82, 0.40}, {0.54, 0.40},
    };

    auto mapped = [&box](const QPointF &pt) {
        return QPointF(box.left() + pt.x() * box.width(),
                       box.top()  + pt.y() * box.height());
    };

    QPainterPath path(mapped(kBolt[0]));
    for (size_t i = 1; i < std::size(kBolt); ++i)
        path.lineTo(mapped(kBolt[i]));
    path.closeSubpath();

    p.save();
    QPen pen(color, 1.2);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(color);
    p.drawPath(path);
    p.restore();
}

/* The Bluetooth rune, on the 24-unit grid icon sets draw it on, so its
 * proportions hold at chip size. The two notches on the right-hand legs are
 * sub-paths: odd-even filling is what punches them out. */
void drawBluetooth(QPainter &p, const QRectF &box, const QColor &color)
{
    static const qreal kRune[][2] = {
        {17.71, 7.71}, {12.00, 2.00}, {11.00, 2.00}, {11.00, 9.59},
        {6.41, 5.00},  {5.00, 6.41},  {10.59, 12.00}, {5.00, 17.59},
        {6.41, 19.00}, {11.00, 14.41}, {11.00, 22.00}, {12.00, 22.00},
        {17.71, 16.29}, {13.41, 12.00},
    };
    static const qreal kNotchUpper[][2] = {{13.00, 5.83}, {14.88, 7.71}, {13.00, 9.59}};
    static const qreal kNotchLower[][2] = {{14.88, 16.29}, {13.00, 18.17}, {13.00, 14.41}};

    const qreal unit = box.width() / 24.0;
    auto mapped = [&box, unit](const qreal pt[2]) {
        return QPointF(box.left() + pt[0] * unit, box.top() + pt[1] * unit);
    };
    auto subPath = [&mapped](const qreal (*pts)[2], size_t count) {
        QPainterPath path(mapped(pts[0]));
        for (size_t i = 1; i < count; ++i)
            path.lineTo(mapped(pts[i]));
        path.closeSubpath();
        return path;
    };

    QPainterPath rune = subPath(kRune, std::size(kRune));
    rune.addPath(subPath(kNotchUpper, std::size(kNotchUpper)));
    rune.addPath(subPath(kNotchLower, std::size(kNotchLower)));

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(rune);
    p.restore();
}

void drawCross(QPainter &p, const QRectF &box, const QColor &color)
{
    p.save();
    QPen pen(color, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(box.topLeft(), box.bottomRight());
    p.drawLine(box.topRight(), box.bottomLeft());
    p.restore();
}

}  // namespace

PopupWidget::PopupWidget(const PopupConfig &config, QObject *parent)
    : QObject(parent)
    , m_layer(new WaylandLayerSurface(this))
    , m_frameTimer(new QTimer(this))
    , m_dismissTimer(new QTimer(this))
    , m_connectTimer(new QTimer(this))
    , m_errorTimer(new QTimer(this))
    , m_connectTimeoutMs(config.connectTimeoutMs)
{
    QScreen *scr = QGuiApplication::primaryScreen();
    m_screenW = scr ? scr->geometry().width() : 1080;

    /* Overlay, so the island grows over the bar's centre instead of racing it
     * for the stacking order of the Top layer. */
    if (!m_layer->init(m_screenW, kSurfaceHeight,
                       WaylandLayerSurface::AnchorTop
                       | WaylandLayerSurface::AnchorLeft
                       | WaylandLayerSurface::AnchorRight,
                       WaylandLayerSurface::Overlay)) {
        qWarning("stylus-popup: layer surface init failed");
    }

    if (!m_theme.loadFromQt6ct())
        qWarning("stylus-popup: using fallback theme colors");

    m_themeWatcher = new QFileSystemWatcher(this);
    connect(m_themeWatcher, &QFileSystemWatcher::fileChanged,
            this, &PopupWidget::onThemeFileChanged);
    connect(m_themeWatcher, &QFileSystemWatcher::directoryChanged,
            this, &PopupWidget::onThemeFileChanged);

    /* Watch the active config file and its directory. The directory watch also
     * catches the config appearing later (e.g. DMS only generates it once it
     * detects qt6ct), and matugen's atomic rewrite of the file. */
    const QString cfgDir = QDir::homePath() + "/.config/qt6ct/colors";
    const QString cfgFile = cfgDir + "/matugen.conf";
    if (QDir().mkpath(cfgDir)) {
        m_themeWatcher->addPath(cfgDir);
        if (QFile::exists(cfgFile))
            m_themeWatcher->addPath(cfgFile);
    }

    const double refreshRate = scr && scr->refreshRate() > 0 ? scr->refreshRate() : 60.0;
    m_frameTimer->setInterval(qMax(1, static_cast<int>(1000.0 / refreshRate)));
    m_frameTimer->setTimerType(Qt::PreciseTimer);
    connect(m_frameTimer, &QTimer::timeout, this, &PopupWidget::onFrame);

    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(kDismissMs);
    connect(m_dismissTimer, &QTimer::timeout, this, &PopupWidget::collapse);

    /* Single shot: re-armed by every wait, so the deadline is counted from the
     * moment the popup started waiting rather than from the last event. */
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, &PopupWidget::onConnectTimeout);

    m_errorTimer->setSingleShot(true);
    m_errorTimer->setInterval(kErrorHoldMs);
    connect(m_errorTimer, &QTimer::timeout, this, &PopupWidget::collapse);

    m_width.configure(kMorphK, kMorphC);
    m_height.configure(kMorphK, kMorphC);
    m_width.epsilon = m_height.epsilon = 0.3;
    m_presence.epsilon = 0.005;
    m_contentAlpha.configure(kFadeK, kFadeC);
    m_contentAlpha.epsilon = 0.005;
    m_shake.configure(kShakeK, kShakeC);
    m_shake.epsilon = 0.2;
    m_pulse.configure(kPulseK, kPulseC);
    m_pulse.epsilon = 0.001;
    m_battery.configure(kFillK, kFillC);
    m_battery.epsilon = 0.2;

    const QFont base = QGuiApplication::font();

    m_titleFont = base;
    m_titleFont.setPixelSize(22);
    m_titleFont.setWeight(QFont::DemiBold);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    /* Tabular figures, so the percentage does not wobble while it counts. */
    m_titleFont.setFeature(QFont::Tag("tnum"), 1);
#endif

    m_subFont = base;
    m_subFont.setPixelSize(12);
    m_subFont.setWeight(QFont::Normal);

    m_compactFont = base;
    m_compactFont.setPixelSize(13);
    m_compactFont.setWeight(QFont::Medium);
}

/* ── state machine ─────────────────────────────────────────────────────── */

bool PopupWidget::isSeated() const
{
    return m_state.attached && m_state.phase == StylusPhase::Complete;
}

void PopupWidget::showState(const StylusState &state)
{
    const bool wasCharging = m_state.charging;

    m_state = state;
    m_dismissTimer->stop();

    if (!state.attached) {
        m_btPaired = false;
        endWaiting(false);  /* the pen detached: a fresh attach may wait again */
        return;
    }

    /* "Attaching" is the driver's only word for "the pen was just seated": it
     * reports nothing at all when the pen leaves the dock, so this event - not
     * a change in the state - is what opens a stage. Being seated again repeats
     * it verbatim, which is precisely the case a given-up wait has to be pulled
     * out of. It is never sent while the pen just sits there, so the driver's
     * periodic chatter still cannot resurrect the popup. */
    if (state.phase == StylusPhase::Attaching) {
        if (m_gaveUp)
            qInfo("PopupWidget: the pen was seated again, so the wait starts over");
        m_btPaired = false;
        m_gaveUp = false;
    }

    if (m_gaveUp)
        return;

    if (!isShown() || m_mode == IslandMode::Error) {
        present();
        return;
    }

    /* The dock reports capacity and charging whether or not the pen is paired,
     * so the expanded view is everything the pen has to say. Pairing is the
     * part BlueZ knows, and the status chip is what says it: worth showing for
     * a pen that is sitting there unpaired, and it covers a fresh pen being
     * paired too - the driver reaches its complete stage long before BlueZ is
     * done with it. */
    setMode(isSeated() ? IslandMode::Expanded : IslandMode::Compact);

    /* Paired: nothing left to wait for, so the pill lingers a moment and then
     * leaves. Unpaired: the deadline owns it, and a dismiss timer would cut
     * that short. */
    m_dismissTimer->stop();
    if (m_btPaired) {
        m_connectTimer->stop();
        m_dismissTimer->start();
    } else if (!m_connectTimer->isActive()) {
        armConnectTimer();
    }

    if (m_content == Content::Battery && state.charging != wasCharging)
        m_pulse.velocity += kPulseKick;
    startFrames();  /* new values to draw */
}

void PopupWidget::onConnectAttemptStarted()
{
    /* An attempt can begin long after the pen was seated: the driver only moves
     * on to its "complete" stage when the charging monitor happens to run next,
     * which may well be past the point where the popup gave up. Without a wait
     * here, that late connection would just appear out of nowhere. */
    if (!m_state.attached)
        return;

    if (m_gaveUp)
        qInfo("PopupWidget: an attempt is starting, so the wait starts over");
    m_gaveUp = false;

    if (!isShown() || m_mode == IslandMode::Error)
        present();
    else if (!m_btPaired && !m_connectTimer->isActive())
        armConnectTimer();
}

void PopupWidget::onBtPaired()
{
    if (!m_state.attached)
        return;

    m_btPaired = true;
    m_gaveUp = false;
    m_connectTimer->stop();

    if (isSeated())
        setMode(IslandMode::Expanded);  /* shows the pill, or just recolours it */
    if (isShown())
        m_dismissTimer->start();        /* nothing left to wait for */
    startFrames();
}

void PopupWidget::onBtConnectionFailed(const QString &error)
{
    qWarning("PopupWidget: Bluetooth connection failed: %s", qPrintable(error));

    /* Nothing retries this attach stage on its own, so stop promising a
     * connection that is not coming. */
    endWaiting(true, IslandStrings::connectFailed());
}

void PopupWidget::onConnectTimeout()
{
    qWarning("PopupWidget: the pen did not connect within %d ms, giving up",
             m_connectTimeoutMs);

    emit connectTimedOut();
    endWaiting(true, m_btPaired ? QString() : IslandStrings::connectTimedOut());
}

void PopupWidget::present()
{
    setMode(isSeated() ? IslandMode::Expanded : IslandMode::Compact);

    if (m_btPaired) {
        m_connectTimer->stop();
        m_dismissTimer->start();
    } else {
        armConnectTimer();
    }
}

void PopupWidget::armConnectTimer()
{
    if (m_connectTimeoutMs <= 0)
        return;  /* 0 switches the timeout off: waiting for the pen never ends */

    m_connectTimer->start(m_connectTimeoutMs);
}

void PopupWidget::stopWaitingTimers()
{
    m_connectTimer->stop();
}

void PopupWidget::endWaiting(bool gaveUp, const QString &reason)
{
    m_gaveUp = gaveUp;
    stopWaitingTimers();

    if (!isShown())
        return;

    /* A reason is worth spelling out only while the pen is not paired: a paired
     * pen has nothing to complain about, and the pill just goes away. */
    if (!reason.isEmpty() && !m_btPaired)
        showError(reason);
    else
        collapse();
}

void PopupWidget::showError(const QString &message)
{
    m_errorText = message;
    m_dismissTimer->stop();
    setMode(IslandMode::Error);
    m_errorTimer->start();
}

void PopupWidget::collapse()
{
    m_dismissTimer->stop();
    m_errorTimer->stop();
    stopWaitingTimers();
    setMode(IslandMode::Hidden);
}

/* ── choreography ──────────────────────────────────────────────────────── */

PopupWidget::Content PopupWidget::contentFor(IslandMode mode)
{
    switch (mode) {
    case IslandMode::Compact:  return Content::Connecting;
    case IslandMode::Expanded: return Content::Battery;
    case IslandMode::Error:    return Content::Error;
    case IslandMode::Hidden:   break;
    }
    return Content::None;
}

void PopupWidget::setMode(IslandMode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;

    if (mode != IslandMode::Error)
        m_errorTimer->stop();
    if (mode == IslandMode::Expanded)
        m_dismissTimer->start();

    if (mode == IslandMode::Hidden && !m_layerVisible)
        return;

    if (!m_layerVisible) {
        /* Every appearance starts as a dot at the top centre. */
        m_layerVisible = true;
        m_layer->setVisibleHeight(kSurfaceHeight);
        m_width.snap(kMinimalSize);
        m_height.snap(kMinimalSize);
        m_presence.snap(0.0);
        m_contentAlpha.snap(0.0);
        m_shake.snap(0.0);
        m_pulse.snap(0.0);
        m_content = Content::None;
    }

    if (mode != IslandMode::Hidden) {
        /* Also catches a collapse that is interrupted halfway. */
        m_collapsing = false;
        m_presence.configure(kMorphK, kMorphC);
        m_presence.target = 1.0;
    }

    /* Old content out first; the shape follows once it has faded. */
    m_nextContent   = contentFor(mode);
    m_swapPending   = true;
    m_revealPending = false;
    m_contentAlpha.target = 0.0;
    startFrames();
}

qreal PopupWidget::compactWidthFor(const QString &text) const
{
    /* The same row the painters lay out, measured instead of drawn: leading
     * inset + icon + gap + label + trailing pad. The 4 px on top are slack, so
     * a label that outgrows its column elides before it touches the pad. */
    const QFontMetricsF fm(m_compactFont);
    const qreal inset = (kCompactHeight - kCompactIcon) / 2;
    return inset + kCompactIcon + kCompactGap + fm.horizontalAdvance(text)
         + kCompactPad + 4;
}

void PopupWidget::applyShapeFor(Content content)
{
    qreal w = kMinimalSize;
    qreal h = kMinimalSize;
    switch (content) {
    case Content::Connecting:
        w = compactWidthFor(IslandStrings::connecting());
        h = kCompactHeight;
        break;
    case Content::Error:
        w = compactWidthFor(m_errorText);
        h = kCompactHeight;
        break;
    case Content::Battery:
        w = kExpandedWidth;
        h = kExpandedHeight;
        m_battery.snap(0.0);  /* the ring fills up as the island opens */
        m_battery.target = m_state.capacity;
        break;
    case Content::None:
        break;
    }
    w = qMin(w, m_screenW - 16.0);

    const bool shrinking = content == Content::None;
    m_width.configure(shrinking ? kSettleK : kMorphK, shrinking ? kSettleC : kMorphC);
    m_height.configure(shrinking ? kSettleK : kMorphK, shrinking ? kSettleC : kMorphC);

    m_fromW = m_width.value;
    m_fromH = m_height.value;
    m_width.target  = w;
    m_height.target = h;
    m_collapsing = shrinking;
}

qreal PopupWidget::shapeProgress() const
{
    auto progress = [](const Spring &s, qreal from) {
        const qreal span = qAbs(s.target - from);
        if (span < 1.0)
            return 1.0;
        return qBound(0.0, 1.0 - qAbs(s.target - s.value) / span, 1.0);
    };
    return qMin(progress(m_width, m_fromW), progress(m_height, m_fromH));
}

void PopupWidget::startFrames()
{
    if (!m_layerVisible || m_frameTimer->isActive())
        return;

    if (!m_clock.isValid())
        m_clock.start();
    m_lastFrameNs = m_clock.nsecsElapsed() - m_frameTimer->interval() * 1000000LL;
    m_frameTimer->start();
}

bool PopupWidget::anythingMoving() const
{
    return m_swapPending || m_revealPending || m_collapsing
        || m_content == Content::Connecting  /* the spinner never rests */
        || !m_width.settled() || !m_height.settled()
        || !m_presence.settled() || !m_contentAlpha.settled()
        || !m_shake.settled() || !m_pulse.settled() || !m_battery.settled();
}

void PopupWidget::onFrame()
{
    const qint64 now = m_clock.nsecsElapsed();
    const qreal dt = (now - m_lastFrameNs) / 1e9;
    m_lastFrameNs = now;

    if (m_swapPending && m_contentAlpha.value < 0.04) {
        m_swapPending = false;
        m_content = m_nextContent;
        applyShapeFor(m_content);
        m_revealPending = m_content != Content::None;
    }

    if (m_revealPending && shapeProgress() >= kRevealAt) {
        m_revealPending = false;
        m_contentAlpha.target = 1.0;
        if (m_content == Content::Error)
            m_shake.velocity = kShakeKick;
    }

    if (m_collapsing && m_presence.target > 0.0 && shapeProgress() >= kVanishAt) {
        m_presence.configure(kSettleK, kSettleC);
        m_presence.target = 0.0;
    }

    if (m_content == Content::Battery)
        m_battery.target = m_state.capacity;

    for (Spring *s : {&m_width, &m_height, &m_presence, &m_contentAlpha,
                      &m_shake, &m_pulse, &m_battery})
        s->step(dt);

    if (m_collapsing && m_presence.target <= 0.0 && m_presence.value < 0.02) {
        finishHide();
        return;
    }

    renderFrame();

    if (!anythingMoving())
        m_frameTimer->stop();
}

void PopupWidget::finishHide()
{
    m_frameTimer->stop();
    m_layer->hide();
    m_layerVisible = false;
    m_collapsing   = false;
    m_content      = Content::None;
    m_presence.snap(0.0);
}

/* ── painting ──────────────────────────────────────────────────────────── */

void PopupWidget::renderFrame()
{
    if (!m_layer->isReady() || !m_layerVisible)
        return;

    const int scale = m_layer->scale();
    if (m_imageBuffer.width() != m_screenW * scale
        || m_imageBuffer.height() != kSurfaceHeight * scale) {
        m_imageBuffer = QImage(m_screenW * scale, kSurfaceHeight * scale,
                               QImage::Format_ARGB32_Premultiplied);
    }
    m_imageBuffer.fill(Qt::transparent);

    {
        QPainter p(&m_imageBuffer);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.scale(scale, scale);
        paintIsland(p);
    }

    m_layer->updateImage(m_imageBuffer);
    m_layer->commitFrame();
}

void PopupWidget::paintIsland(QPainter &p)
{
    const qreal presence = qMax(0.0, m_presence.value);
    const qreal opacity  = qMin(1.0, presence);
    if (opacity <= 0.001)
        return;

    const qreal w = qMax(1.0, m_width.value);
    const qreal h = qMax(1.0, m_height.value);
    const qreal radius = h / 2;
    const QRectF r(-w / 2, -h / 2, w, h);

    p.save();
    p.translate(m_screenW / 2.0 + m_shake.value, kTopMargin + h / 2);
    const qreal scale = (0.6 + 0.4 * presence) * (1.0 + m_pulse.value);
    p.scale(scale, scale);
    p.setOpacity(opacity);

    /* Soft, low elevation shadow: three widening layers, offset downwards. */
    p.setPen(Qt::NoPen);
    static const qreal kShadowAlpha[] = {0.10, 0.07, 0.04};
    for (int i = 0; i < 3; ++i) {
        const qreal spread = 1.5 * (i + 1);
        const QRectF s = r.adjusted(-spread, spread, spread, spread * 2);
        p.setBrush(QColor(0, 0, 0, qRound(255 * kShadowAlpha[i])));
        p.drawRoundedRect(s, radius + spread, radius + spread);
    }

    QPainterPath shape;
    shape.addRoundedRect(r, radius, radius);
    p.fillPath(shape, m_theme.islandFill());
    p.setPen(QPen(m_theme.outline(), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(shape);

    const qreal a = qBound(0.0, m_contentAlpha.value, 1.0);
    if (a > 0.001 && m_content != Content::None) {
        p.setClipPath(shape);
        p.setOpacity(opacity * a);

        /* Content settles into place: a touch smaller and lower while it
         * fades in, the same motion in reverse when it leaves. */
        const qreal cs = 0.92 + 0.08 * a;
        p.translate(0, (1.0 - a) * 4.0);
        p.scale(cs, cs);

        switch (m_content) {
        case Content::Connecting: paintConnecting(p, r); break;
        case Content::Battery:    paintBattery(p, r);    break;
        case Content::Error:      paintError(p, r);      break;
        case Content::None:       break;
        }
    }

    p.restore();
}

void PopupWidget::paintConnecting(QPainter &p, const QRectF &r)
{
    const CompactRow row = compactRowFor(r);
    drawSpinner(p, row.leading);

    p.setFont(m_compactFont);
    p.setPen(m_theme.onSurfaceVariant());
    p.drawText(row.text, Qt::AlignLeft | Qt::AlignVCenter, IslandStrings::connecting());
}

void PopupWidget::paintError(QPainter &p, const QRectF &r)
{
    const CompactRow row = compactRowFor(r);

    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(m_theme.error(), 0.18));
    p.drawEllipse(row.leading);
    drawCross(p, row.leading.adjusted(6.5, 6.5, -6.5, -6.5), m_theme.error());

    p.setFont(m_compactFont);
    p.setPen(m_theme.onSurface());
    p.drawText(row.text, Qt::AlignLeft | Qt::AlignVCenter, m_errorText);
}

void PopupWidget::paintBattery(QPainter &p, const QRectF &r)
{
    /* Leading: the ring sits `pad` in from the rounded end, so its radius is
     * the island's radius minus the padding - a circle concentric with it. */
    const qreal pad  = kExpandedPad;
    const qreal ringD = qMax(24.0, r.height() - 2 * pad);
    const QRectF ring(r.left() + pad, r.center().y() - ringD / 2, ringD, ringD);
    drawBatteryRing(p, ring);

    /* Trailing: the status chip, concentric in the same way. The slot is always
     * drawn - only the glyph changes - so the capsule's right end stays anchored
     * whether or not the pen charges, and the centre column keeps one width.
     * It carries the pairing state: the rune is tinted while the pen is paired
     * and turns amber while it is not. */
    const qreal chipPad = 20;
    const qreal chipD = qMax(16.0, r.height() - 2 * chipPad);
    const QRectF chip(r.right() - chipPad - chipD, r.center().y() - chipD / 2, chipD, chipD);

    const QColor chipColor = m_state.charging ? m_theme.charging()
                          : (m_btPaired ? m_theme.primary() : m_theme.warning());
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(chipColor, m_state.charging ? 0.18 : 0.14));
    p.drawEllipse(chip);

    /* The bolt fills its box; the rune's ink covers only 20 of the 24 units of
     * its grid, so its box is inset less to land at the same optical size. */
    const qreal inset = chipD * (m_state.charging ? 0.25 : 0.21);
    const QRectF glyph = chip.adjusted(inset, inset, -inset, -inset);
    if (m_state.charging)
        drawBolt(p, glyph, chipColor);
    else
        drawBluetooth(p, glyph, chipColor);

    /* Centre: percentage over a one-line status. */
    const qreal textLeft  = ring.right() + 14;
    const qreal textRight = chip.left() - 12;
    const qreal textW     = qMax(0.0, textRight - textLeft);

    QString status = m_state.charging ? IslandStrings::charging()
                   : (m_btPaired ? IslandStrings::paired() : IslandStrings::notPaired());
    if (m_state.limit > 0 && m_state.limit <= 100)
        status += QStringLiteral(" · ") + IslandStrings::chargeLimit(m_state.limit);

    const QFontMetricsF fmTitle(m_titleFont);
    const QFontMetricsF fmSub(m_subFont);
    const qreal gap    = 2;
    const qreal blockH = fmTitle.height() + gap + fmSub.height();
    const qreal top    = r.center().y() - blockH / 2;

    p.setFont(m_titleFont);
    p.setPen(m_theme.onSurface());
    p.drawText(QPointF(textLeft, top + fmTitle.ascent()),
               QStringLiteral("%1%").arg(qBound(0, qRound(m_battery.value), 100)));

    p.setFont(m_subFont);
    p.setPen(m_theme.onSurfaceVariant());
    p.drawText(QPointF(textLeft, top + fmTitle.height() + gap + fmSub.ascent()),
               fmSub.elidedText(status, Qt::ElideRight, textW));
}

void PopupWidget::drawBatteryRing(QPainter &p, const QRectF &r) const
{
    const qreal stroke = 5.0;
    const QRectF arcRect = r.adjusted(stroke / 2 + 1, stroke / 2 + 1,
                                      -stroke / 2 - 1, -stroke / 2 - 1);

    p.save();
    p.setBrush(Qt::NoBrush);

    QPen track(m_theme.progressTrack(), stroke);
    p.setPen(track);
    p.drawEllipse(arcRect);

    const qreal pct = qBound(0.0, m_battery.value, 100.0);
    const QColor arcColor = m_state.charging ? m_theme.charging()
                          : (pct <= 20.0 ? m_theme.lowBattery()
                                                    : m_theme.primary());
    if (pct > 0.5) {
        QPen arc(arcColor, stroke);
        arc.setCapStyle(Qt::RoundCap);
        p.setPen(arc);
        p.drawPath(clockwiseArc(arcRect, 0, 360.0 * pct / 100.0));
    }
    p.restore();

    drawPenGlyph(p, r.center(), r.width() * 0.42, m_theme.onSurfaceVariant());
}

void PopupWidget::drawSpinner(QPainter &p, const QRectF &r) const
{
    /* MD3 indeterminate progress: the arc's head races ahead, then its tail
     * catches up, while the whole thing turns. Each cycle the tail ends 240°
     * further on, so successive cycles join without a jump. */
    constexpr qreal kCycle  = 1.333;
    constexpr qreal kGrowth = 240.0;
    constexpr qreal kMinArc = 30.0;

    const qreal t = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0.0;
    const int   n = static_cast<int>(t / kCycle);
    const qreal phase = std::fmod(t, kCycle) / kCycle;

    qreal head = kGrowth;
    qreal tail = 0.0;
    if (phase < 0.5)
        head = easeInOutCubic(phase * 2) * kGrowth;
    else
        tail = easeInOutCubic((phase - 0.5) * 2) * kGrowth;

    const qreal start = n * kGrowth + t * 120.0 + tail;
    const qreal span  = head - tail + kMinArc;

    const qreal stroke = 2.5;
    const QRectF arcRect = r.adjusted(stroke / 2 + 1, stroke / 2 + 1,
                                      -stroke / 2 - 1, -stroke / 2 - 1);
    p.save();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(withAlpha(m_theme.primary(), 0.14), stroke));
    p.drawEllipse(arcRect);

    QPen arc(m_theme.primary(), stroke);
    arc.setCapStyle(Qt::RoundCap);
    p.setPen(arc);
    p.drawPath(clockwiseArc(arcRect, std::fmod(start, 360.0), span));
    p.restore();
}

void PopupWidget::onThemeFileChanged()
{
    /* Re-arm the file watch: matugen replaces the file atomically, which
     * drops the watch on the path; the directory watch still fires on the
     * rename and on later creation of the config. */
    const QString cfgFile = QDir::homePath() + "/.config/qt6ct/colors/matugen.conf";
    if (QFile::exists(cfgFile))
        m_themeWatcher->addPath(cfgFile);

    if (m_theme.loadFromQt6ct())
        renderFrame();
    else
        qWarning("stylus-popup: theme reload failed, keeping previous colors");
}
