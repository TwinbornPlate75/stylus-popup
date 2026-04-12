#pragma once

#include <QObject>
#include <QTimer>
#include <QEasingCurve>

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

private:
    void slideIn();
    void slideOut();
    void renderFrame();
    void onAnimationTick();

    /* MD3 dark-theme palette — fallback defaults */
    static constexpr QRgb kSurface       = 0xFF211F26;
    static constexpr QRgb kOnSurface     = 0xFFE6E1E5;
    static constexpr QRgb kOnSurfaceVar  = 0xFFCAC4D0;
    static constexpr QRgb kPrimary       = 0xFFD0BCFF;
    static constexpr QRgb kProgressTrack = 0xFF49454F;

    static constexpr int kWidth      = 400;
    static constexpr int kHeight     = 108;
    static constexpr int kAnimMs     = 320;
    static constexpr int kDismissMs  = 4000;

    ColorTheme          m_theme;
    WaylandLayerSurface *m_layer;
    QTimer              *m_animTimer;   /* drives animation at screen refresh rate */
    QTimer              *m_dismissTimer;
    StylusState          m_state;
    bool                 m_shown  = false;

    /* Animation state */
    int                  m_animStart = 0;
    int                  m_animEnd   = 0;
    qint64               m_animBegin = 0;   /* ms since epoch */
    int                  m_animDuration = kAnimMs;
    QEasingCurve         m_animCurve = QEasingCurve::OutExpo;
    bool                 m_animReversed = false;
};
