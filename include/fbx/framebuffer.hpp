#pragma once

#include "cell.hpp"
#include "console.hpp"
#include <vector>
#include <string>
#include <functional>
#include <optional>
#include <memory>

namespace fbx {

// ─── Region ───────────────────────────────────────────────────────────────────

struct Rect {
    int col = 0, row = 0;
    int width = 0, height = 0;

    bool contains(int c, int r) const noexcept {
        return c >= col && c < col + width &&
               r >= row && r < row + height;
    }
    bool valid() const noexcept { return width > 0 && height > 0; }
};

// ─── Snapshot (saved region) ──────────────────────────────────────────────────

/**
 * A saved copy of a rectangular region of a Framebuffer.
 * Used to push/pop terminal regions (e.g. behind dialogs).
 */
struct Snapshot {
    Rect              rect;
    std::vector<Cell> cells; // row-major, [row][col]

    bool valid() const noexcept { return rect.valid() && !cells.empty(); }
};

// ─── Framebuffer ──────────────────────────────────────────────────────────────

/**
 * A 2D grid of Cells representing a terminal framebuffer.
 *
 * Key features:
 *   - get/put cells with UTF-8 grapheme clusters and ANSI attributes
 *   - print strings with embedded escape sequences (color, cursor movement)
 *   - dirty-tracking for efficient diff rendering
 *   - region snapshot (save/restore areas)
 *   - resize with optional content preservation
 *   - compositing (blend another framebuffer on top, respecting transparency)
 */
class Framebuffer {
public:
    // ── Construction ──────────────────────────────────────────────────────────

    Framebuffer();
    explicit Framebuffer(int cols, int rows);
    explicit Framebuffer(Size sz);

    Framebuffer(const Framebuffer&)            = default;
    Framebuffer& operator=(const Framebuffer&) = default;
    Framebuffer(Framebuffer&&)                 = default;
    Framebuffer& operator=(Framebuffer&&)      = default;

    // ── Dimensions ────────────────────────────────────────────────────────────

    int  cols() const noexcept { return m_cols; }
    int  rows() const noexcept { return m_rows; }
    Size size() const noexcept { return {m_cols, m_rows}; }

    /** Resize the buffer, optionally clearing new cells. */
    void resize(int cols, int rows, bool preserve = true);
    void resize(Size sz, bool preserve = true) { resize(sz.cols, sz.rows, preserve); }

    /** Resize to match the current terminal dimensions. */
    void fit_to_terminal();

    // ── Cell access ───────────────────────────────────────────────────────────

    /** Get cell at (col, row). Returns nullopt if out of bounds. */
    std::optional<std::reference_wrapper<Cell>>       at(int col, int row);
    std::optional<std::reference_wrapper<const Cell>> at(int col, int row) const;

    /** Unchecked access. UB if out of bounds. */
    Cell&       cell(int col, int row)       noexcept;
    const Cell& cell(int col, int row) const noexcept;

    /** Put a single cell. No-op if out of bounds. */
    void put(int col, int row, const Cell& c);
    void put(int col, int row, Cell&& c);

    /** Put a character with explicit attributes. */
    void put_char(int col, int row, const std::string& ch,
                  Color fg = Color::none(), Color bg = Color::none(),
                  uint8_t style = Style_None);

    /** Get the character at (col, row) as a UTF-8 string. */
    std::string get_char(int col, int row) const;

    // ── Current attr / cursor ─────────────────────────────────────────────────

    /** Current write cursor position (used by print()). */
    Pos  cursor()     const noexcept { return m_cursor; }
    void set_cursor(int col, int row);
    void set_cursor(Pos p) { set_cursor(p.col, p.row); }
    void move_cursor_rel(int dcol, int drow);

    /** Current foreground/background/style used by print(). */
    Attr&       current_attr()       noexcept { return m_attr; }
    const Attr& current_attr() const noexcept { return m_attr; }
    void set_attr(const Attr& a)             { m_attr = a; }
    void reset_attr()                        { m_attr = {}; }

    // ── High-level print ──────────────────────────────────────────────────────

    /**
     * Print a UTF-8 string at the current cursor position using the
     * current attr.  Handles:
     *   - printable Unicode grapheme clusters (advances cursor)
     *   - \n  → col=0, row++
     *   - \r  → col=0
     *   - \t  → advance to next tab stop (every 8 cols)
     *   - ANSI/VT escape sequences:
     *       ESC[A/B/C/D  cursor movement
     *       ESC[H / ESC[row;colH  cursor position
     *       ESC[m  SGR (colors + style)
     *       ESC[2J  clear screen
     *       ESC[K  erase to end of line
     */
    void print(const std::string& text);
    void print(const char* text);

    // ── Fill / clear ──────────────────────────────────────────────────────────

    void clear(const Cell& fill = Cell{});
    void fill_rect(Rect r, const Cell& fill);

    // ── Region snapshots ──────────────────────────────────────────────────────

    /** Save a rectangular region and return a Snapshot. */
    Snapshot save_region(Rect r) const;

    /** Restore a previously saved region. */
    void restore_region(const Snapshot& snap);

    /** Save the entire buffer. */
    Snapshot save() const;

    /** Restore the entire buffer from a snapshot (must match dimensions). */
    void restore(const Snapshot& snap);

    // ── Compositing ───────────────────────────────────────────────────────────

    /**
     * Composite `other` on top of this buffer at offset (col_off, row_off).
     * Transparent cells in `other` (cell.transparent() == true) are skipped.
     */
    void composite(const Framebuffer& other, int col_off = 0, int row_off = 0);

    // ── Dirty tracking ────────────────────────────────────────────────────────

    bool dirty()      const noexcept { return m_dirty; }
    void mark_clean();
    void mark_all_dirty();

    // ── Rendering ─────────────────────────────────────────────────────────────

    /**
     * Render the framebuffer to stdout using ANSI escape sequences.
     * Only redraws cells marked dirty (differential rendering).
     * Moves cursor to the appropriate positions.
     */
    void render() const;

    /**
     * Render a specific region.
     */
    void render_region(Rect r) const;

    /**
     * Build the full ANSI string for this framebuffer (for testing/piping).
     */
    std::string to_ansi() const;

private:
    int  m_cols = 0;
    int  m_rows = 0;
    bool m_dirty = true;

    std::vector<Cell> m_cells;  // row-major: index = row * m_cols + col
    Pos               m_cursor;
    Attr              m_attr;

    // Used during render() to avoid redundant SGR sequences
    mutable Cell m_last_rendered;

    // Escape sequence parser state
    enum class ParseState { Normal, Escape, CSI };
    struct ParseCtx {
        ParseState state = ParseState::Normal;
        std::string seq;
    };
    mutable ParseCtx m_parse;

    void process_escape(const std::string& seq);
    void advance_cursor();

    bool in_bounds(int col, int row) const noexcept {
        return col >= 0 && col < m_cols && row >= 0 && row < m_rows;
    }
    size_t idx(int col, int row) const noexcept {
        return static_cast<size_t>(row) * m_cols + col;
    }
};

} // namespace fbx

