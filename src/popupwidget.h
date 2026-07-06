#pragma once

#include <QObject>
#include <QTimer>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QImage>
#include <QRect>
#include <QFont>

class QPainter;

#include "colortheme.h"
#include "stylusmonitor.h"
#include "waylandlayersurface.h"

class PopupWidget : public QObject
{
    Q_OBJECT

public:
    explicit PopupWidget(QObject *parent = nullptr);

public slots:
    void showState(const StylusState &state);
    void onBtConnected();
    void onBtConnectionFailed(const QString &error);

private:
    void slideIn(int targetHeight);
    void slideOut();
    void startAnimation(int fromH, int toH, const QEasingCurve &curve);
    void transitionToFinal();
    void renderFrame();
    void onAnimationTick();
    void onSpinnerTick();
    void updateLayoutCache();
    int  targetHeightForState() const;
    bool canShowFinal() const;
    void drawFinalContent(QPainter &p);
    void drawLimitBadge(QPainter &p);

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
    WaylandLayerSurface *m_layer;
    QTimer              *m_animTimer;
    QTimer              *m_dismissTimer;
    QTimer              *m_spinnerTimer;
    StylusState          m_state;
    bool                 m_shown  = false;
    bool                 m_dirty  = true;
    bool                 m_btConnected = false;
    int                  m_screenW = 1080;
    int                  m_spinnerAngle = 0;
    bool                 m_morphing = false;
    qreal                m_morphProgress = 0.0;
    qreal                m_pulsePhase = 0.0;

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
