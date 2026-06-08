#include "fbx/manager.hpp"
#include "fbx/console.hpp"

#include <algorithm>
#include <stdexcept>

namespace fbx {

FramebufferManager::FramebufferManager() {
    m_term_size = console::get_size();
    m_output.resize(m_term_size);
}

FramebufferManager::~FramebufferManager() = default;

// ─── Entry helpers ────────────────────────────────────────────────────────────

FramebufferManager::Entry* FramebufferManager::find_entry(FbId id) {
    for (auto& e : m_buffers)
        if (e.id == id) return &e;
    return nullptr;
}

const FramebufferManager::Entry* FramebufferManager::find_entry(FbId id) const {
    for (const auto& e : m_buffers)
        if (e.id == id) return &e;
    return nullptr;
}

// ─── Buffer lifecycle ─────────────────────────────────────────────────────────

FbId FramebufferManager::create(int cols, int rows, const std::string& name) {
    FbId id = m_next_id++;
    m_buffers.push_back({
        id, name,
        std::make_shared<Framebuffer>(cols, rows),
        false
    });
    return id;
}

FbId FramebufferManager::create_fullscreen(const std::string& name) {
    FbId id = create(m_term_size.cols, m_term_size.rows, name);
    find_entry(id)->fullscreen = true;
    return id;
}

void FramebufferManager::destroy(FbId id) {
    m_buffers.erase(
        std::remove_if(m_buffers.begin(), m_buffers.end(),
                       [id](const Entry& e){ return e.id == id; }),
        m_buffers.end());

    m_layers.erase(
        std::remove_if(m_layers.begin(), m_layers.end(),
                       [id](const Layer& l){ return l.id == id; }),
        m_layers.end());

    if (m_active == id) m_active = FbNull;
}

Framebuffer* FramebufferManager::get(FbId id) {
    auto* e = find_entry(id);
    return e ? e->fb.get() : nullptr;
}

const Framebuffer* FramebufferManager::get(FbId id) const {
    const auto* e = find_entry(id);
    return e ? e->fb.get() : nullptr;
}

FbId FramebufferManager::find(const std::string& name) const {
    for (const auto& e : m_buffers)
        if (e.name == name) return e.id;
    return FbNull;
}

Framebuffer* FramebufferManager::get(const std::string& name) {
    return get(find(name));
}

// ─── Layer mode ───────────────────────────────────────────────────────────────

void FramebufferManager::add_layer(FbId id, int col_off, int row_off, int z_order) {
    auto* e = find_entry(id);
    if (!e) return;

    // Replace if already present
    for (auto& l : m_layers) {
        if (l.id == id) {
            l.col_off = col_off;
            l.row_off = row_off;
            l.z_order = z_order;
            return;
        }
    }

    m_layers.push_back({id, col_off, row_off, z_order, true, 1.0f, e->fb});
}

void FramebufferManager::set_layer_offset(FbId id, int col_off, int row_off) {
    for (auto& l : m_layers)
        if (l.id == id) { l.col_off = col_off; l.row_off = row_off; return; }
}

void FramebufferManager::set_layer_visible(FbId id, bool visible) {
    for (auto& l : m_layers)
        if (l.id == id) { l.visible = visible; return; }
}

void FramebufferManager::set_layer_z(FbId id, int z_order) {
    for (auto& l : m_layers)
        if (l.id == id) { l.z_order = z_order; return; }
}

void FramebufferManager::remove_layer(FbId id) {
    m_layers.erase(
        std::remove_if(m_layers.begin(), m_layers.end(),
                       [id](const Layer& l){ return l.id == id; }),
        m_layers.end());
}

// ─── Tile mode ────────────────────────────────────────────────────────────────

void FramebufferManager::set_tiles(TileLayout layout) {
    m_tiles = std::move(layout);
}

void FramebufferManager::clear_tiles() {
    m_tiles.reset();
}

// ─── Active ───────────────────────────────────────────────────────────────────

void FramebufferManager::set_active(FbId id) {
    m_active = id;
}

// ─── Composition ─────────────────────────────────────────────────────────────

void FramebufferManager::compose_layers() {
    // Sort layers by z_order (ascending → bottom to top)
    std::vector<Layer*> sorted;
    for (auto& l : m_layers) sorted.push_back(&l);
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Layer* a, const Layer* b){ return a->z_order < b->z_order; });

    for (auto* l : sorted) {
        if (!l->visible || !l->fb) continue;
        m_output.composite(*l->fb, l->col_off, l->row_off);
    }
}

void FramebufferManager::compose_tiles() {
    if (!m_tiles) return;
    const auto& layout = *m_tiles;
    int total = m_term_size.cols; // horizontal split by default
    if (layout.dir == TileDir::Vertical) total = m_term_size.rows;

    int weight_sum = 0;
    for (const auto& p : layout.panes) weight_sum += p.weight;
    if (weight_sum == 0) return;

    int offset = 0;
    for (const auto& pane : layout.panes) {
        int size = (total * pane.weight) / weight_sum;
        auto* fb = get(pane.id);
        if (!fb) { offset += size; continue; }

        if (layout.dir == TileDir::Horizontal) {
            fb->resize(size, m_term_size.rows);
            m_output.composite(*fb, offset, 0);
        } else {
            fb->resize(m_term_size.cols, size);
            m_output.composite(*fb, 0, offset);
        }
        offset += size;
    }
}

// ─── Flush ────────────────────────────────────────────────────────────────────

void FramebufferManager::flush() {
    m_output.resize(m_term_size);
    m_output.clear();

    if (m_active != FbNull) {
        // Single active framebuffer mode
        const auto* fb = get(m_active);
        if (fb) m_output.composite(*fb, 0, 0);
    } else if (m_tiles) {
        compose_tiles();
    } else {
        compose_layers();
    }

    m_output.render();
}

void FramebufferManager::invalidate() {
    m_output.mark_all_dirty();
    for (auto& e : m_buffers) e.fb->mark_all_dirty();
}

// ─── Terminal resize ──────────────────────────────────────────────────────────

void FramebufferManager::on_resize() {
    m_term_size = console::get_size();
    m_output.resize(m_term_size);
    m_output.mark_all_dirty();

    for (auto& e : m_buffers) {
        if (e.fullscreen) {
            e.fb->resize(m_term_size);
            e.fb->mark_all_dirty();
        }
    }
}

} // namespace fbx

