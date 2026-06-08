#pragma once

#include <cstdint>
#include <string>
#include <optional>

namespace fbx {

// ─── Terminal size ────────────────────────────────────────────────────────────

struct Size {
    int cols = 80;
    int rows = 24;
};

struct Pos {
    int col = 0; // 0-based
    int row = 0;
};

// ─── Key ─────────────────────────────────────────────────────────────────────

/**
 * Represents a single keypress, parsed from the raw escape sequence.
 *
 * Every key has:
 *   - `raw`     : the exact bytes received from stdin
 *   - `name`    : human-readable label, e.g. "Up", "F5", "Ctrl+C", "a"
 *   - `kind`    : what category of key this is
 *   - modifier flags: ctrl, alt, shift
 *
 * For printable characters, `codepoint` holds the Unicode value.
 * For special keys, `codepoint` is 0.
 *
 * Example:
 *   auto k = console::getch();
 *   if (k.kind == Key::Kind::Arrow && k.name == "Up") { ... }
 *   if (k.is_ctrl('c')) { ... }
 */
struct Key {
    enum class Kind {
        Char,       // Printable Unicode character
        Control,    // Ctrl+letter (codepoint 1-26)
        Arrow,      // Up / Down / Left / Right
        Function,   // F1-F20
        Navigation, // Home, End, PageUp, PageDown, Insert, Delete
        Enter,
        Tab,
        Backspace,
        Escape,
        Unknown,
    };

    std::string raw;        // raw bytes as received
    std::string name;       // human-readable: "a", "Up", "F1", "Ctrl+C", …
    Kind        kind  = Kind::Unknown;
    uint32_t    codepoint = 0; // Unicode codepoint for printable chars, 0 otherwise

    bool mod_ctrl  = false;
    bool mod_alt   = false;  // ESC-prefixed key
    bool mod_shift = false;  // only detectable for some sequences

    // ── Convenience predicates ────────────────────────────────────────────────

    bool is_char()       const noexcept { return kind == Kind::Char; }
    bool is_printable()  const noexcept { return kind == Kind::Char && codepoint >= 0x20; }
    bool is_arrow()      const noexcept { return kind == Kind::Arrow; }
    bool is_function()   const noexcept { return kind == Kind::Function; }
    bool is_enter()      const noexcept { return kind == Kind::Enter; }
    bool is_backspace()  const noexcept { return kind == Kind::Backspace; }
    bool is_escape()     const noexcept { return kind == Kind::Escape; }
    bool is_tab()        const noexcept { return kind == Kind::Tab; }

    /** True if this is Ctrl+<letter>, e.g. is_ctrl('c') for Ctrl+C. */
    bool is_ctrl(char letter) const noexcept {
        if (!mod_ctrl) return false;
        char lo = letter | 0x20; // tolower
        return codepoint == static_cast<uint32_t>(lo - 'a' + 1);
    }

    /** True if this is Alt+<key>. */
    bool is_alt() const noexcept { return mod_alt; }

    /** Function key number (1-based), or 0 if not a function key. */
    int fn() const noexcept {
        if (kind != Kind::Function) return 0;
        // name is "F1".."F20"
        if (name.size() >= 2 && name[0] == 'F') {
            try { return std::stoi(name.substr(1)); } catch (...) {}
        }
        return 0;
    }

    /** UTF-8 string of the character (empty for non-printable). */
    std::string ch() const noexcept {
        return is_printable() ? raw : std::string{};
    }
};

// ─── Console namespace ────────────────────────────────────────────────────────

namespace console {

/**
 * Get the current terminal dimensions.
 * Returns a sensible default (80x24) if the query fails.
 */
Size get_size();

/**
 * Move cursor to absolute 0-based (col, row).
 * Emits ESC[row+1;col+1H to stdout.
 */
void move_cursor(int col, int row);
void move_cursor(Pos p);

/** Hide/show cursor (ESC[?25l / ESC[?25h). */
void hide_cursor();
void show_cursor();

/** Enter/leave alternate screen buffer. */
void enter_alt_screen();
void leave_alt_screen();

/** Enable/disable raw input mode (no echo, no line buffering). */
void set_raw_mode(bool enable);

/** Clear the entire screen (ESC[2J + move to origin). */
void clear_screen();

/** Flush stdout. */
void flush();

// ─── Low-level raw read ───────────────────────────────────────────────────────

/**
 * Read a single key with escape sequence support.
 *
 * Returns the raw byte sequence:
 *   - Printable char : "a", "€", "😀" (UTF-8)
 *   - Control char   : "\x01".."\x1a"
 *   - ESC sequence   : "\x1b[A" (Up), "\x1bOP" (F1), etc.
 *   - Bare ESC       : "\x1b"
 *
 * Timeout in milliseconds (-1 = block forever).
 * Returns nullopt on EOF/error.
 */
std::optional<std::string> read_key(int timeout_ms = -1);

/**
 * Same as read_key but accumulates n raw key sequences.
 */
std::optional<std::string> read_key_n(int n, int timeout_ms = -1);

// ─── High-level parsed read ───────────────────────────────────────────────────

/**
 * Read and parse a single keypress into a Key struct.
 *
 * Requires raw mode to be active (set_raw_mode(true)).
 * Blocks until a key is pressed (timeout_ms = -1).
 *
 * Example:
 *   console::set_raw_mode(true);
 *   auto k = console::getch();
 *   if (k.is_ctrl('q')) quit();
 *   if (k.kind == Key::Kind::Arrow && k.name == "Up") move_up();
 */
Key getch(int timeout_ms = -1);

/**
 * Non-blocking version: returns a Key with kind=Unknown if no input
 * is ready within timeout_ms milliseconds (default 0 = immediate).
 *
 * Equivalent to the classic _getch() / kbhit() combo.
 */
std::optional<Key> _getch(int timeout_ms = 0);

/**
 * Parse a raw byte sequence (as returned by read_key) into a Key.
 * Useful if you already have the raw bytes and just want the semantics.
 */
Key parse_key(const std::string& raw);

// ─── Utilities ────────────────────────────────────────────────────────────────

/**
 * Query cursor position via ESC[6n (DSR).
 * Returns nullopt if the terminal doesn't respond.
 */
std::optional<Pos> query_cursor_pos();

/** Returns true if stdin has data ready (non-blocking poll). */
bool input_ready();

} // namespace console
} // namespace fbx

