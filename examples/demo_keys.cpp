/**
 * demo_keys.cpp
 *
 * Shows each pressed key in real time:
 *   - semantic name (Up, F5, Ctrl+C, …)
 *   - modifiers (Ctrl / Alt / Shift)
 *   - raw bytes in hex
 *   - Unicode codepoint (for printable chars)
 *
 * Exit with Ctrl+Q or Escape.
 */

#include <fbx/fbx.hpp>
#include <sstream>
#include <iomanip>
#include <deque>

using namespace fbx;

// Converts raw bytes to "1B 5B 41" style hex
static std::string to_hex(const std::string& s) {
    std::ostringstream oss;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i) oss << ' ';
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
            << (int)(unsigned char)s[i];
    }
    return oss.str();
}

static std::string kind_name(Key::Kind k) {
    switch (k) {
    case Key::Kind::Char:       return "Char";
    case Key::Kind::Control:    return "Control";
    case Key::Kind::Arrow:      return "Arrow";
    case Key::Kind::Function:   return "Function";
    case Key::Kind::Navigation: return "Navigation";
    case Key::Kind::Enter:      return "Enter";
    case Key::Kind::Tab:        return "Tab";
    case Key::Kind::Backspace:  return "Backspace";
    case Key::Kind::Escape:     return "Escape";
    default:                    return "Unknown";
    }
}

static std::string mod_str(const Key& k) {
    std::string s;
    if (k.mod_ctrl)  s += "Ctrl ";
    if (k.mod_alt)   s += "Alt ";
    if (k.mod_shift) s += "Shift";
    if (s.empty()) s = "—";
    return s;
}

int main() {
    console::enter_alt_screen();
    console::hide_cursor();
    console::set_raw_mode(true);

    Size sz = console::get_size();
    Framebuffer fb(sz);

    // History of the last N pressed keys
    constexpr int HISTORY = 12;
    std::deque<Key> history;

    auto draw = [&]() {
        fb.clear();
        sz = console::get_size();
        fb.resize(sz);

        // ── Title ────────────────────────────────────────────────────────────
        fb.set_cursor(2, 0);
        fb.print("\x1b[1;36m fbx key inspector \x1b[0;2m— Ctrl+Q or Escape to exit\x1b[0m");

        // ── Table header ─────────────────────────────────────────────────────
        int y = 2;
        fb.set_cursor(2, y);
        fb.print("\x1b[1;33m");
        fb.print("  #  ");
        fb.print(std::string(14, ' ').replace(0, 8, "NAME    "));
        fb.print(std::string(12, ' ').replace(0, 6, "KIND  "));
        fb.print(std::string(16, ' ').replace(0, 10, "MODIFIERS "));
        fb.print(std::string(20, ' ').replace(0, 8, "RAW HEX "));
        fb.print("CODEPOINT");
        fb.print("\x1b[0m");

        // Separator line
        ++y;
        fb.set_cursor(2, y);
        fb.print("\x1b[2;37m" + std::string(sz.cols - 4, '-') + "\x1b[0m");
        ++y;

        // ── History ─────────────────────────────────────────────────────────
        int idx = (int)history.size();
        for (auto it = history.rbegin(); it != history.rend() && y < sz.rows - 4; ++it, --idx, ++y) {
            const Key& k = *it;

            // Row color: newest = brighter
            bool newest = (idx == (int)history.size());
            std::string row_color = newest ? "\x1b[0;97m" : "\x1b[0;37m";

            fb.set_cursor(2, y);
            fb.print(row_color);

            // # (index)
            std::ostringstream num;
            num << std::setw(3) << idx;
            fb.print(num.str() + "  ");

            // Name (padded to 14)
            std::string name = k.name;
            if (name.size() > 13) name = name.substr(0, 12) + "…";
            fb.print(name + std::string(14 - std::min((int)name.size(), 13), ' '));

            // Kind (padded to 12)
            std::string kn = kind_name(k.kind);
            fb.print(kn + std::string(12 - kn.size(), ' '));

            // Modifiers (padded to 16)
            std::string ms = mod_str(k);
            fb.print(ms + std::string(16 - ms.size(), ' '));

            // Raw hex (padded to 20)
            std::string hex = to_hex(k.raw);
            if (hex.size() > 18) hex = hex.substr(0, 17) + "…";
            fb.print(hex + std::string(20 - std::min((int)hex.size(), 18), ' '));

            // Codepoint
            if (k.codepoint > 0) {
                std::ostringstream cp;
                cp << "U+" << std::uppercase << std::hex << k.codepoint;
                if (k.is_printable()) cp << "  '" << k.raw << "'";
                fb.print(cp.str());
            } else {
                fb.print("—");
            }

            fb.print("\x1b[0m");
        }

        // ── Last key panel (large, at the bottom) ─────────────────────────────
        int panel_y = sz.rows - 6;
        fb.set_cursor(2, panel_y);
        fb.print("\x1b[2;37m" + std::string(sz.cols - 4, '-') + "\x1b[0m");

        if (!history.empty()) {
            const Key& last = history.back();
            fb.set_cursor(2, panel_y + 1);
            fb.print("\x1b[2;37mLast key:  \x1b[0m");
            fb.print("\x1b[1;97m" + last.name + "\x1b[0m");

            fb.set_cursor(2, panel_y + 2);
            fb.print("\x1b[2;37mRaw bytes:     \x1b[0;33m" + to_hex(last.raw) + "\x1b[0m");

            fb.set_cursor(2, panel_y + 3);
            fb.print("\x1b[2;37mKind:          \x1b[0;36m" + kind_name(last.kind) + "\x1b[0m");

            if (last.codepoint > 0 && last.is_printable()) {
                fb.set_cursor(2, panel_y + 4);
                std::ostringstream cp;
                cp << "\x1b[2;37mChar:          \x1b[0;92m" << last.raw
                   << "  \x1b[2;37m(U+" << std::uppercase << std::hex << last.codepoint << ")\x1b[0m";
                fb.print(cp.str());
            }
        }

        fb.render();
    };

    draw();

    while (true) {
        Key k = console::getch();

        // Exit with Ctrl+Q or Escape
        if (k.is_escape() || k.is_ctrl('q')) break;

        history.push_back(k);
        if ((int)history.size() > HISTORY) history.pop_front();

        draw();
    }

    console::set_raw_mode(false);
    console::show_cursor();
    console::leave_alt_screen();
    return 0;
}

