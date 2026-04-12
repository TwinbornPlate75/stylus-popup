#pragma once

#include <QObject>
#include <QImage>
#include <QTimer>

struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_surface;
struct wl_shm;
struct wl_shm_pool;
struct wl_buffer;
struct zwlr_layer_shell_v1;
struct zwlr_layer_surface_v1;
struct wl_event_queue;

class WaylandLayerSurface : public QObject
{
    Q_OBJECT

public:
    enum Layer  { Background = 0, Bottom = 1, Top = 2, Overlay = 3 };
    enum Anchor { AnchorTop = 1, AnchorBottom = 2, AnchorLeft = 4, AnchorRight = 8 };

    explicit WaylandLayerSurface(QObject *parent = nullptr);
    ~WaylandLayerSurface() override;

    bool init(int width, int height,
              uint32_t anchor = AnchorTop | AnchorLeft | AnchorRight,
              Layer layer     = Top);

    /**
     * Render image to the Wayland surface, sized to current visibleHeight.
     * Only the top visibleHeight rows of the image are displayed.
     */
    void renderImage(const QImage &image);

    /**
     * Detach buffer and commit — makes the surface invisible without
     * destroying it (use after slide-out animation finishes).
     */
    void hide();

    /* Animated property: surface height shown on screen (0 = hidden) */
    void setVisibleHeight(int h);
    int  visibleHeight() const { return m_visibleHeight; }

    int  fullHeight() const { return m_height; }
    int  scale()      const { return m_scale; }   /* device pixel ratio */
    bool isReady()    const { return m_configured; }

signals:
    void configured(int width, int height);

private:
    static void s_registryGlobal(void *data, wl_registry *, uint32_t id,
                                  const char *iface, uint32_t version);
    static void s_registryRemove(void *data, wl_registry *, uint32_t id);
    static void s_lsConfigure(void *data, zwlr_layer_surface_v1 *,
                               uint32_t serial, uint32_t w, uint32_t h);
    static void s_lsClosed(void *data, zwlr_layer_surface_v1 *);

    bool createPool(int width, int height);
    void destroyPool();
    void dispatchQueue();

    wl_display            *m_display      = nullptr;
    wl_event_queue        *m_queue        = nullptr;
    wl_compositor         *m_compositor   = nullptr;
    wl_shm                *m_shm          = nullptr;
    zwlr_layer_shell_v1   *m_layerShell   = nullptr;
    wl_surface            *m_surface      = nullptr;
    zwlr_layer_surface_v1 *m_layerSurf    = nullptr;

    /* Single persistent SHM pool; wl_buffers are created per-frame */
    wl_shm_pool *m_pool   = nullptr;
    uint8_t     *m_data   = nullptr;
    int          m_poolFd = -1;
    int          m_poolSz = 0;

    int  m_width         = 0;
    int  m_height        = 0;
    int  m_visibleHeight = 0;
    int  m_scale         = 1;   /* device pixel ratio (physical px / logical px) */
    bool m_configured    = false;

    QTimer *m_queueTimer = nullptr;
};
