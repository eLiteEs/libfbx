#include "fbx/stream.hpp"
#include "fbx/console.hpp"

#include <cstring>
#include <iostream>

namespace fbx {

// ─── fbxstreambuf ─────────────────────────────────────────────────────────────

fbxstreambuf::fbxstreambuf(Framebuffer& fb) : m_fb(fb) {}

std::streambuf::int_type fbxstreambuf::overflow(int_type c) {
    if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);

    m_pending += (char)traits_type::to_char_type(c);
    flush_pending();
    return c;
}

std::streamsize fbxstreambuf::xsputn(const char* s, std::streamsize n) {
    m_pending.append(s, n);
    flush_pending();
    return n;
}

void fbxstreambuf::flush_pending() {
    if (m_pending.empty()) return;
    m_fb.print(m_pending);
    m_pending.clear();
}

// ─── fbxostream ───────────────────────────────────────────────────────────────

fbxostream::fbxostream(Framebuffer& fb)
    : std::ostream(nullptr), m_buf(fb)
{
    rdbuf(&m_buf);
}

fbxostream& fbxostream::at(int col, int row) {
    m_buf.framebuffer().set_cursor(col, row);
    return *this;
}

fbxostream& fbxostream::set_fg(Color c) {
    m_buf.framebuffer().current_attr().set_fg(c);
    return *this;
}

fbxostream& fbxostream::set_bg(Color c) {
    m_buf.framebuffer().current_attr().set_bg(c);
    return *this;
}

fbxostream& fbxostream::set_style(uint8_t flags) {
    m_buf.framebuffer().current_attr().style = flags;
    return *this;
}

fbxostream& fbxostream::reset_attr() {
    m_buf.framebuffer().reset_attr();
    return *this;
}

// ─── fbxistream ───────────────────────────────────────────────────────────────

// LineBuf::underflow — feeds lines to the istream.
// Each call to underflow runs the line editor in the framebuffer and
// fills the buffer with the result + '\n'.
std::streambuf::int_type fbxistream::LineBuf::underflow() {
    if (m_eof) return traits_type::eof();
    if (m_pos < m_line.size())
        return traits_type::to_int_type(m_line[m_pos]);

    // Run line editor
    m_line = m_owner.readline();
    m_line += '\n';
    m_pos = 0;

    if (m_line == "\n") { m_eof = true; return traits_type::eof(); }
    return traits_type::to_int_type(m_line[m_pos]);
}

fbxistream::fbxistream(Framebuffer& fb, bool echo)
    : std::istream(nullptr), m_fb(fb), m_echo(echo), m_buf(*this)
{
    rdbuf(&m_buf);
}

std::string fbxistream::readline(const std::string& prompt) {
    Framebuffer& fb = m_fb;
    Pos start = fb.cursor();

    // Print prompt
    if (!prompt.empty()) fb.print(prompt);
    Pos input_start = fb.cursor();

    std::string line;
    int cursor_offset = 0; // position within line (0 = end insert)

    // We need the terminal to actually have input; enable raw mode temporarily
    // if it isn't already.  The caller is responsible for raw mode in general,
    // but we make a best-effort here.
    while (true) {
        // Re-render the input area
        {
            Pos p = input_start;
            // Clear old content
            for (int i = input_start.col; i < fb.cols(); ++i)
                fb.put_char(i, p.row, " ");
            // Print current line
            fb.set_cursor(input_start.col, input_start.row);
            if (m_echo) fb.print(line);
            // Draw cursor indicator (block)
            int cur_col = input_start.col + (int)line.size() + cursor_offset;
            if (cur_col < fb.cols())
                fb.put_char(cur_col, input_start.row, "_",
                            Color::bright_white(), Color::none(), Style_Bold);
            fb.render_region({0, input_start.row, fb.cols(), 1});
        }

        auto key = console::read_key(-1);
        if (!key) break;
        const std::string& k = *key;

        if (k == "\r" || k == "\n") {
            break;
        } else if (k == "\x7f" || k == "\x08") {
            // Backspace
            if (!line.empty()) {
                line.pop_back();
            }
        } else if (k == "\x1b[3~") {
            // Delete — no-op for simplicity (same as backspace here)
            if (!line.empty()) line.pop_back();
        } else if (k.size() == 1 && (unsigned char)k[0] >= 0x20) {
            line += k;
        } else if (k.size() > 1 && k[0] != '\x1b') {
            // Multi-byte UTF-8 printable
            line += k;
        }
        // Arrow keys and other escapes: ignored for now
    }

    // Clear the cursor indicator
    {
        int cur_col = input_start.col + (int)line.size();
        if (cur_col < fb.cols())
            fb.put_char(cur_col, input_start.row, " ");
    }

    fb.set_cursor(start.col, start.row + 1);
    return line;
}

} // namespace fbx 

