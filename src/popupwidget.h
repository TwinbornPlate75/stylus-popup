#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QImage>
#include <QRectF>
#include <QFont>

class QFileSystemWatcher;
class QPainter;

#include "colortheme.h"
#include "popupconfig.h"
#include "spring.h"
#include "stylusmonitor.h"
#include "waylandlayersurface.h"

/**
 * The pen's status island: a pill at the top centre of the screen that grows
 * out of a small dot, morphs between a compact "connecting" form and an
 * expanded battery view, and shrinks back into the dot before it disappears.
 *
 * Every animated quantity is a `Spring`, driven by one frame loop that only
 * runs while something is moving. Shape and content are choreographed apart:
 * the old content fades out, the shape morphs, and the new content fades in
 * once the shape has mostly arrived.
 */
class PopupWidget : public QObject
{
    Q_OBJECT

public:
    explicit PopupWidget(const PopupConfig &config, QObject *parent = nullptr);

public slots:
    void showState(const StylusState &state);

    /**
     * A connection attempt is now running. Opens a wait for it when this attach
     * stage has none left, which is what happens when the attempt only starts
     * after the popup gave up on the pen.
     */
    void onConnectAttemptStarted();

    void onBtPaired();
    void onBtConnectionFailed(const QString &error);

signals:
    /**
     * The pen did not connect within `connect-timeout-ms`. The popup has
     * already given up on it; this is the cue to drop the pending attempt.
     */
    void connectTimedOut();

private slots:
    void onConnectTimeout();

private:
    /** What the island is asked to be. */
    enum class IslandMode { Hidden, Compact, Expanded, Error };

    /** What is painted inside it; lags behind the mode while it cross-fades. */
    enum class Content { None, Connecting, Battery, Error };

    /* ── state machine (what to show) ── */
    bool isShown() const { return m_mode != IslandMode::Hidden; }

    /** The pen is seated and the driver is reporting, so the expanded battery
     *  view has real numbers in it. Whether the pen is also paired is a
     *  separate fact: the one the status chip carries. */
    bool isSeated() const;

    void present();
    void collapse();
    void showError(const QString &message);

    /** Starts the wait window, unless the timeout is switched off. */
    void armConnectTimer();

    /** Stops the timer that only runs while the pen is on its way. */
    void stopWaitingTimers();

    /** Ends the wait and takes the island off screen. `gaveUp` records whether
     *  this attach stage is over for good (no connection is coming) or was
     *  merely interrupted (the pen detached, so a fresh attach may wait). A
     *  non-empty `reason` is shown briefly before the island goes away. */
    void endWaiting(bool gaveUp, const QString &reason = QString());

    /* ── choreography (how it moves) ── */
    void setMode(IslandMode mode);
    static Content contentFor(IslandMode mode);
    void applyShapeFor(Content content);
    qreal shapeProgress() const;
    void startFrames();
    void onFrame();
    bool anythingMoving() const;
    void finishHide();

    /* ── painting ── */
    void renderFrame();
    void paintIsland(QPainter &p);
    void paintConnecting(QPainter &p, const QRectF &r);
    void paintBattery(QPainter &p, const QRectF &r);
    void paintError(QPainter &p, const QRectF &r);
    void drawSpinner(QPainter &p, const QRectF &r) const;
    void drawBatteryRing(QPainter &p, const QRectF &r) const;
    qreal compactWidthFor(const QString &text) const;
    void onThemeFileChanged();

    /* Geometry, in logical pixels. The island's corner radius is always half
     * its height, and everything inside follows from that radius minus the
     * padding, so inner shapes stay concentric with the outline. */
    static constexpr int kSurfaceHeight  = 104;
    static constexpr int kTopMargin      = 8;
    static constexpr int kMinimalSize    = 36;
    static constexpr int kCompactHeight  = 36;
    static constexpr int kExpandedWidth  = 320;
    static constexpr int kExpandedHeight = 76;
    static constexpr int kExpandedPad    = 12;
    static constexpr int kDismissMs      = 4000;
    static constexpr int kErrorHoldMs    = 1400;

    ColorTheme          m_theme;
    QFileSystemWatcher *m_themeWatcher;
    WaylandLayerSurface *m_layer;
    QTimer             *m_frameTimer;
    QTimer             *m_dismissTimer;
    QTimer             *m_connectTimer;
    QTimer             *m_errorTimer;
    StylusState         m_state;
    /* The pen's pairing state, and the only thing BlueZ is asked about. It is
     * dropped on every attach - a fresh seat has to earn it again, which is
     * what leaves the chip showing "not paired" while a pen is being paired. */
    bool                m_btPaired = false;
    bool                m_gaveUp = false;
    int                 m_connectTimeoutMs = PopupConfig::kDefaultConnectTimeoutMs;
    int                 m_screenW = 1080;

    IslandMode m_mode        = IslandMode::Hidden;
    Content    m_content     = Content::None;
    Content    m_nextContent = Content::None;
    bool       m_swapPending   = false;  /* waiting for old content to fade */
    bool       m_revealPending = false;  /* waiting for the shape to arrive */
    bool       m_collapsing    = false;
    bool       m_layerVisible  = false;
    QString    m_errorText;

    Spring m_width;         /* island size */
    Spring m_height;
    Spring m_presence;      /* 0 = gone, 1 = fully there: scale and opacity */
    Spring m_contentAlpha;  /* content fade, 0..1 */
    Spring m_shake;         /* horizontal offset, kicked on errors */
    Spring m_pulse;         /* extra scale, kicked on charging changes */
    Spring m_battery;       /* displayed capacity, 0..100 */
    qreal  m_fromW = 0.0;
    qreal  m_fromH = 0.0;

    QElapsedTimer m_clock;      /* since the frame loop started; spinner phase */
    qint64        m_lastFrameNs = 0;

    QImage m_imageBuffer;
    QFont  m_titleFont;
    QFont  m_subFont;
    QFont  m_compactFont;
};
