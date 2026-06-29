#pragma once

#include <QObject>
#include <QTimer>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QImage>
#include <QRect>
#include <QFont>
#include <QPainterPath>
#include <QPen>

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
    void updateStatusText();
    int  targetHeightForState() const;
    bool canShowFinal() const;

    static constexpr int kWidth         = 280;
    static constexpr int kHeight        = 92;
    static constexpr int kWaitingHeight = 50;
    static constexpr int kAnimMs        = 320;
    static constexpr int kDismissMs     = 4000;
    static constexpr int kBarH          = 5;
    static constexpr int kIconW         = 28;
    static constexpr int kIconH         = 44;
    static constexpr int kBatW          = 20;
    static constexpr int kBatH          = 32;
    static constexpr int kPad           = 14;
    static constexpr int kSpinnerSize   = 20;
    static constexpr int kSpinnerMs     = 16;

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

    int m_animStart = 0;
    int m_animEnd   = 0;
    QElapsedTimer m_elapsed;
    QEasingCurve m_animCurve;

    QImage m_imageBuffer;
    QRect  m_cardRect;
    QRect  m_waitingCardRect;
    QPainterPath m_cardPath;
    QPainterPath m_waitingCardPath;
    QPen   m_borderPen;
    int    m_textX = 0;
    int    m_titleY = 0;
    int    m_subY   = 0;
    int    m_barW   = 0;
    int    m_barY   = 0;
    int    m_iconX  = 0;
    int    m_iconY  = 0;
    int    m_batX   = 0;
    int    m_batY   = 0;

    QFont  m_titleFont;
    QFont  m_subFont;
    QFont  m_limitFont;
    QString m_statusText;
    QString m_limitText;
};
