#pragma once

#include <QObject>
#include <QImage>
#include <QTimer>

struct wl_buffer;
struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_surface;
struct wl_shm;
struct wl_shm_pool;
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
     * Re-upload the image content into the persistent SHM buffer.
     * No-op if the image is byte-identical to the previously uploaded one
     * (compared via QImage::cacheKey), so callers can invoke this
     * every frame cheaply.
     */
    void updateImage(const QImage &image);

    /**
     * Re-attach the SHM buffer and commit, with damage covering the
     * current visibleHeight. Cheap; the same wl_buffer is reused.
     */
    void commitFrame();

    /**
     * Detach buffer and commit — makes the surface invisible without
     * destroying it (use after slide-out animation finishes).
     */
    void hide();

    /* Animated property: surface height shown on screen (0 = hidden) */
    void setVisibleHeight(int h) { m_visibleHeight = h; }
    int  visibleHeight() const { return m_visibleHeight; }
    int  scale()      const { return m_scale; }   /* device pixel ratio */
    bool isReady()    const { return m_configured; }

private:
    static void s_registryGlobal(void *data, wl_registry *, uint32_t id,
                                  const char *iface, uint32_t version);
    static void s_registryRemove(void *data, wl_registry *, uint32_t id);
    static void s_lsConfigure(void *data, zwlr_layer_surface_v1 *,
                               uint32_t serial, uint32_t w, uint32_t h);
    static void s_lsClosed(void *data, zwlr_layer_surface_v1 *);

    bool createPool(int width, int height);
    void destroyPool();
    bool ensurePoolSize(int physW, int physH);
    bool ensureBuffer(int physW, int physH, int stride);

    wl_display            *m_display      = nullptr;
    wl_event_queue        *m_queue        = nullptr;
    wl_compositor         *m_compositor   = nullptr;
    wl_shm                *m_shm          = nullptr;
    zwlr_layer_shell_v1   *m_layerShell   = nullptr;
    wl_surface            *m_surface      = nullptr;
    zwlr_layer_surface_v1 *m_layerSurf    = nullptr;

    /* SHM pool sized to the full image (reused across frames). The wl_buffer
     * itself is cropped to the current visH and recreated on size change. */
    wl_shm_pool *m_pool   = nullptr;
    wl_buffer   *m_buffer = nullptr;
    uint8_t     *m_data   = nullptr;
    int          m_poolFd = -1;
    int          m_poolSz = 0;
    int          m_bufW   = 0;
    int          m_bufH   = 0;

    int  m_width         = 0;
    int  m_height        = 0;
    int  m_visibleHeight = 0;
    int  m_scale         = 1;   /* device pixel ratio (physical px / logical px) */
    int  m_committedHeight = 0; /* last size sent to zwlr_layer_surface_v1_set_size */
    qint64 m_lastImageKey = 0;  /* QImage::cacheKey of the last upload */
    bool m_configured    = false;

    QTimer *m_queueTimer = nullptr;
};
