#pragma once

#include <QObject>
#include <QTimer>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QImage>
#include <QRect>
#include <QFont>

class QFileSystemWatcher;
class QPainter;

#include "colortheme.h"
#include "popupconfig.h"
#include "stylusmonitor.h"
#include "waylandlayersurface.h"

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

    void onBtConnected();
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
    void slideIn();
    void slideOut();
    void startAnimation(int fromH, int toH, const QEasingCurve &curve);
    void transitionToFinal();
    void renderFrame();
    void onAnimationTick();
    void onSpinnerTick();
    void onThemeFileChanged();
    void updateLayoutCache();
    int  targetHeightForState() const;
    bool canShowFinal() const;
    void drawFinalContent(QPainter &p);
    void drawLimitBadge(QPainter &p);
    QRect spinnerRectFor(const QRect &container) const;

    /** Starts the wait window, unless the timeout is switched off. */
    void armConnectTimer();

    /** Stops the two timers that only run while the pen is on its way. */
    void stopWaitingTimers();

    /** Ends the wait and takes the popup off screen. `gaveUp` records whether
     *  this attach stage is over for good (no connection is coming) or was
     *  merely interrupted (the pen detached, so a fresh attach may wait). */
    void endWaiting(bool gaveUp);

    static constexpr int kSurfaceHeight   = 110;
    static constexpr int kCapsuleWidth    = 260;
    static constexpr int kCapsuleHeight   = 66;
    static constexpr int kCapsuleTopMargin= 4;
    static constexpr int kWaitingWidth    = 132;
    static constexpr int kWaitingHeight   = 38;
    static constexpr int kBatteryGlyphSize= 44;
    static constexpr int kAnimMs          = 280;
    static constexpr int kDismissMs       = 4000;
    static constexpr int kSpinnerSize     = 22;
    static constexpr int kSpinnerTextGap  = 8;
    static constexpr int kSpinnerMs       = 16;

    ColorTheme          m_theme;
    QFileSystemWatcher  *m_themeWatcher;
    WaylandLayerSurface *m_layer;
    QTimer              *m_animTimer;
    QTimer              *m_dismissTimer;
    QTimer              *m_spinnerTimer;
    QTimer              *m_connectTimer;
    StylusState          m_state;
    bool                 m_shown  = false;
    bool                 m_dirty  = true;
    bool                 m_btConnected = false;
    bool                 m_gaveUp = false;
    int                  m_connectTimeoutMs = PopupConfig::kDefaultConnectTimeoutMs;
    int                  m_screenW = 1080;
    int                  m_spinnerAngle = 0;
    bool                 m_morphing = false;
    qreal                m_morphProgress = 0.0;

    int m_animStart = 0;
    int m_animEnd   = 0;
    QElapsedTimer m_elapsed;
    QEasingCurve m_animCurve;

    QImage m_imageBuffer;

    QRect        m_capsuleRect;
    QRect        m_waitingChipRect;
    QRect        m_glyphRect;
    QRect        m_textRect;
    QRect        m_limitRect;

    QFont  m_titleFont;
    QFont  m_subFont;
};
