#include "fbx/cell.hpp"
#include <sstream>

namespace fbx {

// ─── Color ────────────────────────────────────────────────────────────────────

bool Color::operator==(const Color& o) const noexcept {
    if (mode != o.mode) return false;
    switch (mode) {
    case ColorMode::Default:   return true;
    case ColorMode::Color256:  return value.index == o.value.index;
    case ColorMode::TrueColor: return value.rgb.r == o.value.rgb.r &&
                                      value.rgb.g == o.value.rgb.g &&
                                      value.rgb.b == o.value.rgb.b;
    }
    return false;
}

// ─── Cell ─────────────────────────────────────────────────────────────────────

Cell::Cell(std::string grapheme, Color fg_, Color bg_, uint8_t style_)
    : ch(std::move(grapheme)), fg(fg_), bg(bg_), style(style_), dirty(true)
{
    // Estimate width: very naive; a proper implementation would use
    // wcwidth() or a Unicode table.
    width = ch.empty() ? 0 : 1;
}

bool Cell::operator==(const Cell& o) const noexcept {
    return ch == o.ch && fg == o.fg && bg == o.bg && style == o.style;
}

// Build ANSI SGR prefix for this cell.
// If prev is provided, only emit differences.
std::string Cell::ansi_prefix(const Cell* prev) const {
    std::string out;

    // We always need to emit something if there's no previous cell.
    bool need_reset = (prev == nullptr);
    bool fg_changed = !prev || fg != prev->fg;
    bool bg_changed = !prev || bg != prev->bg;
    bool st_changed = !prev || style != prev->style;

    if (!fg_changed && !bg_changed && !st_changed) return "";

    out += "\x1b[";

    std::string parts;
    auto add = [&](const std::string& s) {
        if (!parts.empty()) parts += ';';
        parts += s;
    };

    if (need_reset || st_changed) {
        add("0");
        // After reset we must re-emit everything
        fg_changed = true;
        bg_changed = true;
    }

    // Style flags
    if (style & Style_Bold)      add("1");
    if (style & Style_Dim)       add("2");
    if (style & Style_Italic)    add("3");
    if (style & Style_Underline) add("4");
    if (style & Style_Blink)     add("5");
    if (style & Style_Reverse)   add("7");
    if (style & Style_Strike)    add("9");

    // Foreground
    if (fg_changed) {
        switch (fg.mode) {
        case ColorMode::Default:
            add("39");
            break;
        case ColorMode::Color256:
            if (fg.value.index < 8)       add(std::to_string(30 + fg.value.index));
            else if (fg.value.index < 16) add(std::to_string(90 + fg.value.index - 8));
            else {
                add("38");
                add("5");
                add(std::to_string(fg.value.index));
            }
            break;
        case ColorMode::TrueColor:
            add("38");
            add("2");
            add(std::to_string(fg.value.rgb.r));
            add(std::to_string(fg.value.rgb.g));
            add(std::to_string(fg.value.rgb.b));
            break;
        }
    }

    // Background
    if (bg_changed) {
        switch (bg.mode) {
        case ColorMode::Default:
            add("49");
            break;
        case ColorMode::Color256:
            if (bg.value.index < 8)       add(std::to_string(40 + bg.value.index));
            else if (bg.value.index < 16) add(std::to_string(100 + bg.value.index - 8));
            else {
                add("48");
                add("5");
                add(std::to_string(bg.value.index));
            }
            break;
        case ColorMode::TrueColor:
            add("48");
            add("2");
            add(std::to_string(bg.value.rgb.r));
            add(std::to_string(bg.value.rgb.g));
            add(std::to_string(bg.value.rgb.b));
            break;
        }
    }

    out += parts;
    out += 'm';
    return out;
}

} // namespace fbx

