#pragma once

#include "framebuffer.hpp"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace fbx {

using FbId = int;
static constexpr FbId FbNull = -1;

// ─── Layer entry ──────────────────────────────────────────────────────────────

struct Layer {
    FbId         id      = FbNull;
    int          col_off = 0;  // top-left offset within output
    int          row_off = 0;
    int          z_order = 0;  // lower = rendered first (bottom)
    bool         visible = true;
    float        opacity = 1.0f; // reserved; currently transparent-cell only

    std::shared_ptr<Framebuffer> fb;
};

// ─── Tile layout ──────────────────────────────────────────────────────────────

enum class TileDir { Horizontal, Vertical };

struct TilePane {
    FbId id     = FbNull;
    int  weight = 1; // proportional size
};

struct TileLayout {
    TileDir             dir    = TileDir::Horizontal;
    std::vector<TilePane> panes;
};

// ─── FramebufferManager ───────────────────────────────────────────────────────

/**
 * Manages a collection of named framebuffers.
 *
 * Supports two display modes:
 *
 *  1. LAYER mode — framebuffers are composited on top of each other
 *     according to their z_order and col/row offsets.
 *
 *  2. TILE mode — the terminal is divided into regions (H or V split),
 *     each showing a different framebuffer.
 *
 * `flush()` composites all visible layers into a single output buffer
 * and renders it to the terminal.
 */
class FramebufferManager {
public:
    FramebufferManager();
    ~FramebufferManager();

    // ── Buffer lifecycle ──────────────────────────────────────────────────────

    /** Create a new framebuffer and return its ID. */
    FbId create(int cols, int rows, const std::string& name = "");
    FbId create(Size sz,           const std::string& name = "") { return create(sz.cols, sz.rows, name); }

    /** Create a framebuffer sized to fill the terminal. */
    FbId create_fullscreen(const std::string& name = "");

    /** Remove a framebuffer and all associated layer entries. */
    void destroy(FbId id);

    /** Get a framebuffer by ID (returns nullptr if not found). */
    Framebuffer*       get(FbId id);
    const Framebuffer* get(FbId id) const;

    /** Lookup by name. */
    FbId               find(const std::string& name) const;
    Framebuffer*       get(const std::string& name);

    // ── Layer mode ────────────────────────────────────────────────────────────

    /**
     * Add a framebuffer to the layer stack.
     * `z_order` controls depth (lower = drawn first).
     */
    void add_layer(FbId id, int col_off = 0, int row_off = 0, int z_order = 0);

    void set_layer_offset(FbId id, int col_off, int row_off);
    void set_layer_visible(FbId id, bool visible);
    void set_layer_z(FbId id, int z_order);

    void remove_layer(FbId id);

    // ── Tile mode ─────────────────────────────────────────────────────────────

    /**
     * Split the terminal into N tiles.
     * Each TilePane maps a FbId to a proportional weight.
     * The manager resizes each framebuffer to fit its tile.
     */
    void set_tiles(TileLayout layout);
    void clear_tiles();

    // ── Output ────────────────────────────────────────────────────────────────

    /**
     * Choose which framebuffer to display as the sole output.
     * Disables layer and tile modes for that flush.
     */
    void set_active(FbId id);
    FbId active() const noexcept { return m_active; }

    /**
     * Composite all visible layers (or apply tiling) into the output
     * framebuffer and render to the terminal.
     */
    void flush();

    /**
     * Force a full redraw of the output on next flush().
     */
    void invalidate();

    // ── Terminal resize ───────────────────────────────────────────────────────

    /**
     * Call when the terminal is resized (e.g. SIGWINCH handler).
     * Resizes all fullscreen buffers and the output buffer.
     */
    void on_resize();

    Size terminal_size() const { return m_term_size; }

private:
    struct Entry {
        FbId                         id;
        std::string                  name;
        std::shared_ptr<Framebuffer> fb;
        bool                         fullscreen = false;
    };

    FbId m_next_id = 1;
    std::vector<Entry>  m_buffers;
    std::vector<Layer>  m_layers;    // sorted by z_order during flush
    std::optional<TileLayout> m_tiles;
    FbId                m_active = FbNull;
    Size                m_term_size;

    // The composed output buffer
    Framebuffer m_output;

    Entry*       find_entry(FbId id);
    const Entry* find_entry(FbId id) const;

    void compose_layers();
    void compose_tiles();
};

} // namespace fbx

