#pragma once

/**
 * Presentation settings: the `[popup]` section of stylus-popup's config. Filled
 * in by `AppConfig::load()`, which owns the file; the popup only reads.
 */
struct PopupConfig {
    /** Largest accepted `connect-timeout-ms`; anything above is clamped to it. */
    static constexpr int kMaxConnectTimeoutMs = 600000;

    /** Shipped default, also the value written into a fresh config file. */
    static constexpr int kDefaultConnectTimeoutMs = 15000;

    /**
     * How long the pen has to connect after it attaches, counted from the
     * moment the popup starts waiting for it. When the wait runs out the popup
     * slides away and the pending connection attempt is dropped, so nothing
     * retries that attach stage on its own - the next attach, or the next
     * stage of this one, opens a fresh window. `0` waits forever.
     */
    int connectTimeoutMs = kDefaultConnectTimeoutMs;
};
