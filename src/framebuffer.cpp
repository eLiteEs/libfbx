#include "fbx/framebuffer.hpp"
#include "fbx/console.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <sstream>

namespace fbx {

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Returns byte-count of a UTF-8 sequence starting with byte b.
static int utf8_len(unsigned char b) {
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1; // continuation or invalid → treat as 1
}

// Advance `it` past one UTF-8 grapheme, return the cluster as string.
static std::string next_grapheme(const char*& it, const char* end) {
    if (it >= end) return {};
    int len = utf8_len((unsigned char)*it);
    len = std::min(len, (int)(end - it));
    std::string g(it, it + len);
    it += len;
    return g;
}

// Parse a decimal integer from a string, returning 1 if empty (CSI default).
static int csi_int(const std::string& s, int def = 1) {
    if (s.empty()) return def;
    try { return std::stoi(s); } catch (...) { return def; }
}

// ─── Construction ─────────────────────────────────────────────────────────────

Framebuffer::Framebuffer() : Framebuffer(0, 0) {}

Framebuffer::Framebuffer(int cols, int rows)
    : m_cols(cols), m_rows(rows)
    , m_cells(static_cast<size_t>(cols) * rows)
{}

Framebuffer::Framebuffer(Size sz) : Framebuffer(sz.cols, sz.rows) {}

// ─── Resize ───────────────────────────────────────────────────────────────────

void Framebuffer::resize(int cols, int rows, bool preserve) {
    if (cols == m_cols && rows == m_rows) return;

    std::vector<Cell> new_cells(static_cast<size_t>(cols) * rows);

    if (preserve) {
        int copy_cols = std::min(cols, m_cols);
        int copy_rows = std::min(rows, m_rows);
        for (int r = 0; r < copy_rows; ++r)
            for (int c = 0; c < copy_cols; ++c)
                new_cells[r * cols + c] = m_cells[idx(c, r)];
    }

    m_cols  = cols;
    m_rows  = rows;
    m_cells = std::move(new_cells);
    mark_all_dirty();
}

void Framebuffer::fit_to_terminal() {
    resize(console::get_size());
}

// ─── Cell access ─────────────────────────────────────────────────────────────

std::optional<std::reference_wrapper<Cell>> Framebuffer::at(int col, int row) {
    if (!in_bounds(col, row)) return std::nullopt;
    return m_cells[idx(col, row)];
}

std::optional<std::reference_wrapper<const Cell>> Framebuffer::at(int col, int row) const {
    if (!in_bounds(col, row)) return std::nullopt;
    return m_cells[idx(col, row)];
}

Cell& Framebuffer::cell(int col, int row) noexcept {
    return m_cells[idx(col, row)];
}

const Cell& Framebuffer::cell(int col, int row) const noexcept {
    return m_cells[idx(col, row)];
}

void Framebuffer::put(int col, int row, const Cell& c) {
    if (!in_bounds(col, row)) return;
    auto& dst = m_cells[idx(col, row)];
    if (dst != c) {
        dst = c;
        dst.dirty = true;
        m_dirty = true;
    }
}

void Framebuffer::put(int col, int row, Cell&& c) {
    if (!in_bounds(col, row)) return;
    auto& dst = m_cells[idx(col, row)];
    c.dirty = (dst != c);
    if (c.dirty) m_dirty = true;
    dst = std::move(c);
}

void Framebuffer::put_char(int col, int row, const std::string& ch,
                            Color fg, Color bg, uint8_t style) {
    put(col, row, Cell{ch, fg, bg, style});
}

std::string Framebuffer::get_char(int col, int row) const {
    if (!in_bounds(col, row)) return {};
    return m_cells[idx(col, row)].ch;
}

// ─── Cursor ───────────────────────────────────────────────────────────────────

void Framebuffer::set_cursor(int col, int row) {
    m_cursor = {
        std::clamp(col, 0, m_cols > 0 ? m_cols - 1 : 0),
        std::clamp(row, 0, m_rows > 0 ? m_rows - 1 : 0)
    };
}

void Framebuffer::move_cursor_rel(int dcol, int drow) {
    set_cursor(m_cursor.col + dcol, m_cursor.row + drow);
}

// ─── Print + escape sequence parser ──────────────────────────────────────────

void Framebuffer::advance_cursor() {
    m_cursor.col++;
    if (m_cursor.col >= m_cols) {
        m_cursor.col = 0;
        m_cursor.row++;
        if (m_cursor.row >= m_rows) m_cursor.row = m_rows - 1;
    }
}

// Parse and apply a complete CSI sequence (without the ESC[ prefix).
// `seq` is everything between ESC[ and the final byte (inclusive).
void Framebuffer::process_escape(const std::string& seq) {
    if (seq.empty()) return;
    char cmd = seq.back();
    std::string params = seq.substr(0, seq.size() - 1);

    // Split params on ';'
    auto split = [&]() -> std::vector<std::string> {
        std::vector<std::string> v;
        std::stringstream ss(params);
        std::string tok;
        while (std::getline(ss, tok, ';')) v.push_back(tok);
        return v;
    };

    switch (cmd) {
    // ── Cursor movement ──────────────────────────────────────────────────────
    case 'A': move_cursor_rel(0, -csi_int(params));                  break; // up
    case 'B': move_cursor_rel(0,  csi_int(params));                  break; // down
    case 'C': move_cursor_rel( csi_int(params), 0);                  break; // right
    case 'D': move_cursor_rel(-csi_int(params), 0);                  break; // left
    case 'E': set_cursor(0, m_cursor.row + csi_int(params));         break; // next line
    case 'F': set_cursor(0, m_cursor.row - csi_int(params));         break; // prev line
    case 'G': set_cursor(csi_int(params) - 1, m_cursor.row);         break; // column
    case 'H': case 'f': {
        auto ps = split();
        int r = ps.size() > 0 ? csi_int(ps[0]) - 1 : 0;
        int c = ps.size() > 1 ? csi_int(ps[1]) - 1 : 0;
        set_cursor(c, r);
        break;
    }

    // ── Erase ────────────────────────────────────────────────────────────────
    case 'J': {
        int n = csi_int(params, 0);
        if (n == 2 || n == 3) {
            clear();
        } else if (n == 0) {
            // erase from cursor to end
            for (int c = m_cursor.col; c < m_cols; ++c)
                put(c, m_cursor.row, Cell{});
            for (int r = m_cursor.row + 1; r < m_rows; ++r)
                for (int c = 0; c < m_cols; ++c)
                    put(c, r, Cell{});
        } else if (n == 1) {
            // erase from start to cursor
            for (int r = 0; r < m_cursor.row; ++r)
                for (int c = 0; c < m_cols; ++c)
                    put(c, r, Cell{});
            for (int c = 0; c <= m_cursor.col; ++c)
                put(c, m_cursor.row, Cell{});
        }
        break;
    }
    case 'K': {
        int n = csi_int(params, 0);
        int start = (n == 1) ? 0             : m_cursor.col;
        int end   = (n == 0) ? m_cols - 1    : m_cursor.col;
        if (n == 2) { start = 0; end = m_cols - 1; }
        for (int c = start; c <= end; ++c) put(c, m_cursor.row, Cell{});
        break;
    }

    // ── SGR (Select Graphic Rendition) ───────────────────────────────────────
    case 'm': {
        auto ps = split();
        if (ps.empty() || (ps.size() == 1 && ps[0].empty())) {
            m_attr = {};
            break;
        }
        for (size_t i = 0; i < ps.size(); ++i) {
            int p = csi_int(ps[i], 0);
            switch (p) {
            case 0:  m_attr = {};                            break;
            case 1:  m_attr.style |= Style_Bold;             break;
            case 2:  m_attr.style |= Style_Dim;              break;
            case 3:  m_attr.style |= Style_Italic;           break;
            case 4:  m_attr.style |= Style_Underline;        break;
            case 5:  m_attr.style |= Style_Blink;            break;
            case 7:  m_attr.style |= Style_Reverse;          break;
            case 9:  m_attr.style |= Style_Strike;           break;
            case 22: m_attr.style &= ~(Style_Bold|Style_Dim);break;
            case 23: m_attr.style &= ~Style_Italic;          break;
            case 24: m_attr.style &= ~Style_Underline;       break;
            case 25: m_attr.style &= ~Style_Blink;           break;
            case 27: m_attr.style &= ~Style_Reverse;         break;
            case 29: m_attr.style &= ~Style_Strike;          break;
            case 39: m_attr.fg = Color::none();              break;
            case 49: m_attr.bg = Color::none();              break;
            // Standard fg 30-37, bright 90-97
            default:
                if (p >= 30 && p <= 37)   { m_attr.fg = Color::palette(p - 30);       }
                else if (p >= 40 && p <= 47) { m_attr.bg = Color::palette(p - 40);    }
                else if (p >= 90 && p <= 97) { m_attr.fg = Color::palette(p - 90 + 8);}
                else if (p >= 100 && p <= 107){ m_attr.bg = Color::palette(p-100+8);  }
                else if (p == 38 && i + 2 < ps.size()) {
                    int mode = csi_int(ps[i+1], 0);
                    if (mode == 5 && i + 2 < ps.size()) {
                        m_attr.fg = Color::palette(csi_int(ps[i+2]));
                        i += 2;
                    } else if (mode == 2 && i + 4 < ps.size()) {
                        m_attr.fg = Color::rgb(csi_int(ps[i+2]),
                                               csi_int(ps[i+3]),
                                               csi_int(ps[i+4]));
                        i += 4;
                    }
                }
                else if (p == 48 && i + 2 < ps.size()) {
                    int mode = csi_int(ps[i+1], 0);
                    if (mode == 5 && i + 2 < ps.size()) {
                        m_attr.bg = Color::palette(csi_int(ps[i+2]));
                        i += 2;
                    } else if (mode == 2 && i + 4 < ps.size()) {
                        m_attr.bg = Color::rgb(csi_int(ps[i+2]),
                                               csi_int(ps[i+3]),
                                               csi_int(ps[i+4]));
                        i += 4;
                    }
                }
                break;
            }
        }
        break;
    }

    default:
        break; // Unknown sequence — ignore
    }
}

void Framebuffer::print(const std::string& text) {
    print(text.c_str());
}

void Framebuffer::print(const char* text) {
    const char* it  = text;
    const char* end = text + std::strlen(text);

    while (it < end) {
        unsigned char c = (unsigned char)*it;

        // ── ESC ──────────────────────────────────────────────────────────────
        if (c == 0x1b) {
            ++it;
            if (it >= end) break;
            char next = *it;
            ++it;

            if (next == '[') {
                // CSI — read until final byte [0x40-0x7E]
                std::string seq;
                while (it < end) {
                    char b = *it++;
                    seq += b;
                    if (b >= 0x40 && b <= 0x7E) break;
                }
                process_escape(seq);
            }
            // Other ESC sequences (OSC, SS3, etc.) — skip for now
            continue;
        }

        // ── Control characters ───────────────────────────────────────────────
        if (c == '\n') {
            m_cursor.col = 0;
            m_cursor.row++;
            if (m_cursor.row >= m_rows) m_cursor.row = m_rows - 1;
            ++it;
            continue;
        }
        if (c == '\r') {
            m_cursor.col = 0;
            ++it;
            continue;
        }
        if (c == '\t') {
            int next_tab = (m_cursor.col / 8 + 1) * 8;
            while (m_cursor.col < next_tab && m_cursor.col < m_cols) {
                put(m_cursor.col, m_cursor.row, Cell{" ", m_attr.fg, m_attr.bg, m_attr.style});
                m_cursor.col++;
            }
            ++it;
            continue;
        }
        if (c < 0x20) { ++it; continue; } // Other control chars — skip

        // ── Printable grapheme ────────────────────────────────────────────────
        std::string g = next_grapheme(it, end);
        if (g.empty()) break;

        if (in_bounds(m_cursor.col, m_cursor.row)) {
            put(m_cursor.col, m_cursor.row,
                Cell{g, m_attr.fg, m_attr.bg, m_attr.style});
        }
        advance_cursor();
    }
}

// ─── Fill / clear ─────────────────────────────────────────────────────────────

void Framebuffer::clear(const Cell& fill) {
    for (auto& c : m_cells) c = fill;
    mark_all_dirty();
}

void Framebuffer::fill_rect(Rect r, const Cell& fill) {
    for (int row = r.row; row < r.row + r.height; ++row)
        for (int col = r.col; col < r.col + r.width; ++col)
            put(col, row, fill);
}

// ─── Snapshots ────────────────────────────────────────────────────────────────

Snapshot Framebuffer::save_region(Rect r) const {
    Snapshot snap;
    snap.rect = r;
    snap.cells.reserve(r.width * r.height);
    for (int row = r.row; row < r.row + r.height; ++row)
        for (int col = r.col; col < r.col + r.width; ++col) {
            if (in_bounds(col, row))
                snap.cells.push_back(m_cells[idx(col, row)]);
            else
                snap.cells.push_back(Cell{});
        }
    return snap;
}

void Framebuffer::restore_region(const Snapshot& snap) {
    if (!snap.valid()) return;
    const Rect& r = snap.rect;
    size_t i = 0;
    for (int row = r.row; row < r.row + r.height; ++row)
        for (int col = r.col; col < r.col + r.width; ++col, ++i)
            put(col, row, snap.cells[i]);
}

Snapshot Framebuffer::save() const {
    return save_region({0, 0, m_cols, m_rows});
}

void Framebuffer::restore(const Snapshot& snap) {
    restore_region(snap);
}

// ─── Compositing ──────────────────────────────────────────────────────────────

void Framebuffer::composite(const Framebuffer& other, int col_off, int row_off) {
    for (int r = 0; r < other.m_rows; ++r) {
        for (int c = 0; c < other.m_cols; ++c) {
            const Cell& src = other.m_cells[other.idx(c, r)];
            if (src.transparent()) continue;
            put(c + col_off, r + row_off, src);
        }
    }
}

// ─── Dirty tracking ───────────────────────────────────────────────────────────

void Framebuffer::mark_clean() {
    m_dirty = false;
    for (auto& c : m_cells) c.dirty = false;
}

void Framebuffer::mark_all_dirty() {
    m_dirty = true;
    for (auto& c : m_cells) c.dirty = true;
}

// ─── Rendering ────────────────────────────────────────────────────────────────

void Framebuffer::render() const {
    render_region({0, 0, m_cols, m_rows});
    // Reset SGR at end
    std::fputs("\x1b[0m", stdout);
    console::flush();
    const_cast<Framebuffer*>(this)->mark_clean();
}

void Framebuffer::render_region(Rect r) const {
    const Cell* prev = nullptr;
    for (int row = r.row; row < r.row + r.height && row < m_rows; ++row) {
        for (int col = r.col; col < r.col + r.width && col < m_cols; ++col) {
            const Cell& c = m_cells[idx(col, row)];
            if (!c.dirty) continue;

            console::move_cursor(col, row);

            std::string sgr = c.ansi_prefix(prev);
            if (!sgr.empty()) std::fputs(sgr.c_str(), stdout);

            std::fputs(c.ch.empty() ? " " : c.ch.c_str(), stdout);
            prev = &c;
        }
    }
}

std::string Framebuffer::to_ansi() const {
    std::string out;
    out += "\x1b[H"; // move to origin
    const Cell* prev = nullptr;
    for (int row = 0; row < m_rows; ++row) {
        for (int col = 0; col < m_cols; ++col) {
            const Cell& c = m_cells[idx(col, row)];
            out += c.ansi_prefix(prev);
            out += c.ch.empty() ? " " : c.ch;
            prev = &c;
        }
        if (row + 1 < m_rows) out += "\r\n";
    }
    out += "\x1b[0m";
    return out;
}

} // namespace fbx

