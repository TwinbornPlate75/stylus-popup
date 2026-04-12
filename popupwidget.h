#pragma once

#include <QObject>
#include <QImage>
#include <QPropertyAnimation>
#include <QTimer>

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

    /* MD3 dark-theme palette */
    static constexpr QRgb kSurface       = 0xFF211F26;
    static constexpr QRgb kOnSurface     = 0xFFE6E1E5;
    static constexpr QRgb kOnSurfaceVar  = 0xFFCAC4D0;
    static constexpr QRgb kPrimary       = 0xFFD0BCFF;
    static constexpr QRgb kProgressTrack = 0xFF49454F;

    static constexpr int kWidth      = 400;
    static constexpr int kHeight     = 108;
    static constexpr int kAnimMs     = 320;
    static constexpr int kDismissMs  = 4000;

    WaylandLayerSurface *m_layer;
    QPropertyAnimation  *m_anim;        /* animates m_layer::visibleHeight */
    QTimer              *m_dismissTimer;
    StylusState          m_state;
    bool                 m_shown  = false;
};
