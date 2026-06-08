#pragma once

#include "framebuffer.hpp"
#include <streambuf>
#include <ostream>
#include <istream>
#include <string>

namespace fbx {

// ─── fbxstreambuf ─────────────────────────────────────────────────────────────

/**
 * A std::streambuf that writes to a Framebuffer.
 *
 * Characters written to this streambuf are forwarded to
 * Framebuffer::print(), so escape sequences, newlines, tabs etc. are
 * handled exactly as they would be in print().
 *
 * Usage:
 *   fbxstreambuf sb(fb);
 *   std::ostream os(&sb);
 *   os << "\033[1mHello\033[0m, world!\n";
 */
class fbxstreambuf : public std::streambuf {
public:
    explicit fbxstreambuf(Framebuffer& fb);

    Framebuffer&       framebuffer()       noexcept { return m_fb; }
    const Framebuffer& framebuffer() const noexcept { return m_fb; }

protected:
    // Called for each character overflow (no buffer — direct passthrough)
    int_type overflow(int_type c) override;

    // Called for multi-char writes
    std::streamsize xsputn(const char* s, std::streamsize n) override;

private:
    Framebuffer& m_fb;
    std::string  m_pending; // accumulates bytes until a full grapheme
    void flush_pending();
};

// ─── fbxostream ───────────────────────────────────────────────────────────────

/**
 * std::ostream that writes to a Framebuffer.
 *
 *   fbxostream fout(fb);
 *   fout << Color::red() << "Error: " << Color::none() << msg << '\n';
 *
 * Supports stream manipulators:
 *   fout << fbx::fg(Color::red()) << "text" << fbx::reset;
 */
class fbxostream : public std::ostream {
public:
    explicit fbxostream(Framebuffer& fb);

    Framebuffer&       framebuffer()       noexcept { return m_buf.framebuffer(); }
    const Framebuffer& framebuffer() const noexcept { return m_buf.framebuffer(); }

    /** Move cursor to (col, row) — chainable manipulator. */
    fbxostream& at(int col, int row);
    fbxostream& at(Pos p) { return at(p.col, p.row); }

    /** Set current fg color. */
    fbxostream& set_fg(Color c);
    fbxostream& set_bg(Color c);
    fbxostream& set_style(uint8_t flags);
    fbxostream& reset_attr();

private:
    fbxstreambuf m_buf;
};

// ─── Stream manipulators ──────────────────────────────────────────────────────

namespace manip {

struct FgManip  { Color c; };
struct BgManip  { Color c; };
struct AtManip  { Pos p; };
struct StyleManip { uint8_t flags; };

inline FgManip fg(Color c)       { return {c}; }
inline BgManip bg(Color c)       { return {c}; }
inline AtManip at(int col, int row) { return {{col, row}}; }
inline AtManip at(Pos p)         { return {p}; }

inline fbxostream& operator<<(fbxostream& os, FgManip m)  { return os.set_fg(m.c); }
inline fbxostream& operator<<(fbxostream& os, BgManip m)  { return os.set_bg(m.c); }
inline fbxostream& operator<<(fbxostream& os, AtManip m)  { return os.at(m.p); }
inline fbxostream& operator<<(fbxostream& os, StyleManip m) { return os.set_style(m.flags); }

inline fbxostream& reset(fbxostream& os) { return os.reset_attr(); }

} // namespace manip

using manip::fg;
using manip::bg;
using manip::at;

// ─── fbxistream — stdin reader that works with a visible framebuffer ──────────

/**
 * An input helper that reads from stdin while keeping the framebuffer
 * in a consistent state.
 *
 * Problem: std::cin / getline pull raw bytes from stdin.  When the
 * terminal is in raw or alt-screen mode the built-in echo and line
 * discipline are bypassed.  fbxistream handles:
 *   - Displaying a simple line editor at the current cursor position
 *     inside the framebuffer (with backspace/delete support)
 *   - Returning the finished string to the caller
 *   - Restoring the cursor and dirty-marking the affected region
 *
 * Usage:
 *   fbxistream fin(fb);
 *   std::string line;
 *   fin >> line;   // or:  std::getline(fin, line);
 */
class fbxistream : public std::istream {
public:
    explicit fbxistream(Framebuffer& fb, bool echo = true);

    /**
     * Read a line of input, displaying it in the framebuffer.
     * Returns the entered string (without the trailing newline).
     */
    std::string readline(const std::string& prompt = "");

    Framebuffer& framebuffer() noexcept { return m_fb; }

private:
    Framebuffer& m_fb;
    bool         m_echo;

    // Minimal streambuf that reads from our line editor
    class LineBuf : public std::streambuf {
    public:
        explicit LineBuf(fbxistream& owner) : m_owner(owner) {}
    protected:
        int_type underflow() override;
    private:
        fbxistream& m_owner;
        std::string m_line;
        std::size_t m_pos = 0;
        bool        m_eof = false;
    };

    LineBuf m_buf;
};

} // namespace fbx 

