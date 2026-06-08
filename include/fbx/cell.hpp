#pragma once

#include <string>
#include <cstdint>

namespace fbx {

// ─── Color ────────────────────────────────────────────────────────────────────

enum class ColorMode : uint8_t {
    Default,   // terminal default
    Color256,  // 256-color palette
    TrueColor, // 24-bit RGB
};

struct Color {
    ColorMode mode = ColorMode::Default;
    union {
        uint8_t  index;       // Color256
        struct { uint8_t r, g, b; } rgb; // TrueColor
    } value = {};

    static Color none()                               { return {}; }
    static Color palette(uint8_t idx)                 { Color c; c.mode = ColorMode::Color256;  c.value.index = idx; return c; }
    static Color rgb(uint8_t r, uint8_t g, uint8_t b) { Color c; c.mode = ColorMode::TrueColor; c.value.rgb = {r,g,b}; return c; }

    // Named 16-color helpers (standard ANSI palette index)
    static Color black()          { return palette(0);  }
    static Color red()            { return palette(1);  }
    static Color green()          { return palette(2);  }
    static Color yellow()         { return palette(3);  }
    static Color blue()           { return palette(4);  }
    static Color magenta()        { return palette(5);  }
    static Color cyan()           { return palette(6);  }
    static Color white()          { return palette(7);  }
    static Color bright_black()   { return palette(8);  }
    static Color bright_red()     { return palette(9);  }
    static Color bright_green()   { return palette(10); }
    static Color bright_yellow()  { return palette(11); }
    static Color bright_blue()    { return palette(12); }
    static Color bright_magenta() { return palette(13); }
    static Color bright_cyan()    { return palette(14); }
    static Color bright_white()   { return palette(15); }

    bool operator==(const Color& o) const noexcept;
    bool operator!=(const Color& o) const noexcept { return !(*this == o); }
};

// ─── Style flags ──────────────────────────────────────────────────────────────

enum StyleFlags : uint8_t {
    Style_None      = 0,
    Style_Bold      = 1 << 0,
    Style_Dim       = 1 << 1,
    Style_Italic    = 1 << 2,
    Style_Underline = 1 << 3,
    Style_Blink     = 1 << 4,
    Style_Reverse   = 1 << 5,
    Style_Strike    = 1 << 6,
};

// ─── Cell ─────────────────────────────────────────────────────────────────────

/**
 * A single terminal cell.
 *
 * `ch` stores the UTF-8 encoded grapheme cluster (can be multi-byte).
 * An empty `ch` means "transparent" (used for compositing).
 *
 * `width` is the visual column width of the character:
 *   1 for normal chars, 2 for wide (CJK, emoji), 0 for combining.
 */
struct Cell {
    std::string ch      = " ";  // UTF-8 grapheme cluster
    Color       fg      = Color::none();
    Color       bg      = Color::none();
    uint8_t     style   = Style_None;
    uint8_t     width   = 1;    // display width (1 or 2)
    bool        dirty   = true; // needs redraw

    Cell() = default;
    explicit Cell(std::string grapheme, Color fg = Color::none(),
                  Color bg = Color::none(), uint8_t style = Style_None);

    bool transparent() const noexcept { return ch.empty(); }
    bool operator==(const Cell& o) const noexcept;
    bool operator!=(const Cell& o) const noexcept { return !(*this == o); }

    // Build ANSI escape prefix for this cell's style.
    // Pass the previous cell to emit only diffs (nullptr = reset first).
    std::string ansi_prefix(const Cell* prev = nullptr) const;

    static Cell transparent_cell() { Cell c; c.ch = ""; c.width = 0; return c; }
    static Cell space()            { return Cell{}; }
};

// ─── Attr helper ──────────────────────────────────────────────────────────────

/**
 * Convenience builder for cell attributes.
 *
 *   Attr a = Attr{}.fg(Color::red()).bold();
 */
struct Attr {
    Color   fg    = Color::none();
    Color   bg    = Color::none();
    uint8_t style = Style_None;

    Attr& set_fg(Color c)    { fg = c;                    return *this; }
    Attr& set_bg(Color c)    { bg = c;                    return *this; }
    Attr& bold()             { style |= Style_Bold;       return *this; }
    Attr& dim()              { style |= Style_Dim;        return *this; }
    Attr& italic()           { style |= Style_Italic;     return *this; }
    Attr& underline()        { style |= Style_Underline;  return *this; }
    Attr& blink()            { style |= Style_Blink;      return *this; }
    Attr& reverse()          { style |= Style_Reverse;    return *this; }
    Attr& strike()           { style |= Style_Strike;     return *this; }

    Cell apply(std::string ch, uint8_t w = 1) const {
        return Cell{std::move(ch), fg, bg, style};
        (void)w;
    }
};

} // namespace fbx

