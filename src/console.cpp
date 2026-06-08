#include "fbx/console.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <optional>
#include <vector>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <io.h>
#else
  #include <sys/ioctl.h>
  #include <termios.h>
  #include <unistd.h>
  #include <poll.h>
  #include <cstdlib>
#endif

namespace fbx::console {

// ─── Terminal size ────────────────────────────────────────────────────────────

Size get_size() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
        int cols = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
        int rows = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
        return {cols, rows};
    }
#else
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return {ws.ws_col, ws.ws_row};
#endif
    return {80, 24};
}

// ─── Cursor ───────────────────────────────────────────────────────────────────

void move_cursor(int col, int row) {
    // ANSI is 1-based; our API is 0-based
    std::printf("\x1b[%d;%dH", row + 1, col + 1);
}

void move_cursor(Pos p) { move_cursor(p.col, p.row); }

void hide_cursor() { std::fputs("\x1b[?25l", stdout); }
void show_cursor() { std::fputs("\x1b[?25h", stdout); }

// ─── Alt screen ───────────────────────────────────────────────────────────────

void enter_alt_screen() { std::fputs("\x1b[?1049h", stdout); flush(); }
void leave_alt_screen() { std::fputs("\x1b[?1049l", stdout); flush(); }

// ─── Raw mode ─────────────────────────────────────────────────────────────────

#ifdef _WIN32
static DWORD s_orig_in_mode  = 0;
static DWORD s_orig_out_mode = 0;

void set_raw_mode(bool enable) {
    HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (enable) {
        GetConsoleMode(hin,  &s_orig_in_mode);
        GetConsoleMode(hout, &s_orig_out_mode);

        DWORD in_mode  = s_orig_in_mode;
        DWORD out_mode = s_orig_out_mode;

        // Disable echo + line input, enable virtual terminal processing
        in_mode  &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        in_mode  |= ENABLE_VIRTUAL_TERMINAL_INPUT;
        out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;

        SetConsoleMode(hin,  in_mode);
        SetConsoleMode(hout, out_mode);
    } else {
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE),  s_orig_in_mode);
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), s_orig_out_mode);
    }
}

#else
static struct termios s_orig_termios;

void set_raw_mode(bool enable) {
    if (enable) {
        tcgetattr(STDIN_FILENO, &s_orig_termios);
        struct termios raw = s_orig_termios;
        cfmakeraw(&raw);
        // Keep OPOST so \n still works on output
        raw.c_oflag |= OPOST;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    } else {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_orig_termios);
    }
}
#endif

// ─── Clear + flush ────────────────────────────────────────────────────────────

void clear_screen() {
    std::fputs("\x1b[2J\x1b[H", stdout);
    flush();
}

void flush() { std::fflush(stdout); }

// ─── Input ready ─────────────────────────────────────────────────────────────

bool input_ready() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD n = 0;
    return PeekConsoleInput(h, nullptr, 0, &n) && n > 0;
#else
    struct pollfd pfd{ STDIN_FILENO, POLLIN, 0 };
    return ::poll(&pfd, 1, 0) > 0;
#endif
}

// ─── read_key ─────────────────────────────────────────────────────────────────

static int read_byte(int timeout_ms) {
#ifdef _WIN32
    // On Windows we just do a blocking ReadConsoleInput for simplicity.
    // A real implementation would use WaitForSingleObject for timeout.
    (void)timeout_ms;
    INPUT_RECORD ir{};
    DWORD n = 0;
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    while (true) {
        ReadConsoleInput(h, &ir, 1, &n);
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            char c = ir.Event.KeyEvent.uChar.AsciiChar;
            if (c) return (unsigned char)c;
        }
    }
#else
    if (timeout_ms >= 0) {
        struct pollfd pfd{ STDIN_FILENO, POLLIN, 0 };
        int r = ::poll(&pfd, 1, timeout_ms);
        if (r <= 0) return -1;
    }
    unsigned char c;
    ssize_t r = ::read(STDIN_FILENO, &c, 1);
    return (r == 1) ? (int)c : -1;
#endif
}

std::optional<std::string> read_key(int timeout_ms) {
    int c = read_byte(timeout_ms);
    if (c < 0) return std::nullopt;

    std::string seq;
    seq += (char)c;

    if (c == 0x1b) {
        // ESC — try to read the rest of the sequence with a short timeout
        int c2 = read_byte(20);
        if (c2 < 0) return seq; // bare ESC

        seq += (char)c2;

        if (c2 == '[' || c2 == 'O') {
            // CSI or SS3 — read until we hit a letter (final byte 0x40-0x7E)
            for (int i = 0; i < 16; ++i) {
                int cn = read_byte(20);
                if (cn < 0) break;
                seq += (char)cn;
                if (cn >= 0x40 && cn <= 0x7E) break;
            }
        }
        return seq;
    }

    // Multi-byte UTF-8
    if ((c & 0x80) != 0) {
        int extra = 0;
        if      ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;

        for (int i = 0; i < extra; ++i) {
            int cn = read_byte(20);
            if (cn < 0) break;
            seq += (char)cn;
        }
    }

    return seq;
}

std::optional<std::string> read_key_n(int n, int timeout_ms) {
    std::string result;
    for (int i = 0; i < n; ++i) {
        auto k = read_key(timeout_ms);
        if (!k) return std::nullopt;
        result += *k;
    }
    return result;
}

// ─── parse_key ────────────────────────────────────────────────────────────────

// Decode a UTF-8 sequence to its Unicode codepoint.
static uint32_t utf8_to_codepoint(const std::string& s) {
    if (s.empty()) return 0;
    unsigned char c = s[0];
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && s.size() >= 2)
        return ((c & 0x1F) << 6)  | (s[1] & 0x3F);
    if ((c & 0xF0) == 0xE0 && s.size() >= 3)
        return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    if ((c & 0xF8) == 0xF0 && s.size() >= 4)
        return ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12)
             | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    return c;
}

Key parse_key(const std::string& raw) {
    Key k;
    k.raw = raw;

    if (raw.empty()) {
        k.kind = Key::Kind::Unknown;
        k.name = "";
        return k;
    }

    unsigned char c0 = raw[0];

    // ── Bare ESC ─────────────────────────────────────────────────────────────
    if (raw == "\x1b") {
        k.kind = Key::Kind::Escape;
        k.name = "Escape";
        return k;
    }

    // ── ESC-prefixed (Alt+key) ────────────────────────────────────────────────
    // If the sequence starts with ESC but isn't a CSI/SS3 sequence,
    // it's Alt+<next char>.
    if (c0 == 0x1b && raw.size() >= 2 && raw[1] != '[' && raw[1] != 'O') {
        // Parse the rest as a plain key, then set mod_alt
        Key inner = parse_key(raw.substr(1));
        inner.raw = raw;
        inner.mod_alt = true;
        inner.name = "Alt+" + inner.name;
        return inner;
    }

    // ── CSI sequences  ESC [ ... ─────────────────────────────────────────────
    if (c0 == 0x1b && raw.size() >= 3 && raw[1] == '[') {
        std::string body = raw.substr(2); // everything after ESC[
        char final_byte  = body.back();
        std::string params = body.substr(0, body.size() - 1);

        // Helper: split params on ';'
        auto split_params = [&]() {
            std::vector<std::string> v;
            std::string tok;
            for (char ch : params) {
                if (ch == ';') { v.push_back(tok); tok.clear(); }
                else tok += ch;
            }
            v.push_back(tok);
            return v;
        };
        auto ps = split_params();
        auto pi = [&](int i, int def = 1) -> int {
            if (i >= (int)ps.size() || ps[i].empty()) return def;
            try { return std::stoi(ps[i]); } catch (...) { return def; }
        };

        // Modifier encoding in CSI sequences: param2 - 1
        // 1=none 2=Shift 3=Alt 4=Shift+Alt 5=Ctrl 6=Shift+Ctrl 7=Alt+Ctrl 8=all
        int mod_param = (ps.size() >= 2) ? pi(1) : 1;
        if (mod_param > 1) {
            int m = mod_param - 1;
            k.mod_shift = m & 1;
            k.mod_alt   = m & 2;
            k.mod_ctrl  = m & 4;
        }

        switch (final_byte) {
        // Arrow keys
        case 'A': k.kind = Key::Kind::Arrow;      k.name = "Up";       return k;
        case 'B': k.kind = Key::Kind::Arrow;      k.name = "Down";     return k;
        case 'C': k.kind = Key::Kind::Arrow;      k.name = "Right";    return k;
        case 'D': k.kind = Key::Kind::Arrow;      k.name = "Left";     return k;
        case 'E': k.kind = Key::Kind::Navigation; k.name = "Begin";    return k; // numpad 5
        case 'F': k.kind = Key::Kind::Navigation; k.name = "End";      return k;
        case 'H': k.kind = Key::Kind::Navigation; k.name = "Home";     return k;
        case 'Z': k.kind = Key::Kind::Tab;        k.name = "Shift+Tab"; k.mod_shift = true; return k;

        case '~': {
            int n = pi(0);
            switch (n) {
            case 1:  case 7:  k.kind = Key::Kind::Navigation; k.name = "Home";    return k;
            case 2:           k.kind = Key::Kind::Navigation; k.name = "Insert";  return k;
            case 3:           k.kind = Key::Kind::Navigation; k.name = "Delete";  return k;
            case 4:  case 8:  k.kind = Key::Kind::Navigation; k.name = "End";     return k;
            case 5:           k.kind = Key::Kind::Navigation; k.name = "PageUp";  return k;
            case 6:           k.kind = Key::Kind::Navigation; k.name = "PageDown";return k;
            // Function keys via CSI n ~
            case 11: k.kind = Key::Kind::Function; k.name = "F1";  return k;
            case 12: k.kind = Key::Kind::Function; k.name = "F2";  return k;
            case 13: k.kind = Key::Kind::Function; k.name = "F3";  return k;
            case 14: k.kind = Key::Kind::Function; k.name = "F4";  return k;
            case 15: k.kind = Key::Kind::Function; k.name = "F5";  return k;
            case 17: k.kind = Key::Kind::Function; k.name = "F6";  return k;
            case 18: k.kind = Key::Kind::Function; k.name = "F7";  return k;
            case 19: k.kind = Key::Kind::Function; k.name = "F8";  return k;
            case 20: k.kind = Key::Kind::Function; k.name = "F9";  return k;
            case 21: k.kind = Key::Kind::Function; k.name = "F10"; return k;
            case 23: k.kind = Key::Kind::Function; k.name = "F11"; return k;
            case 24: k.kind = Key::Kind::Function; k.name = "F12"; return k;
            case 25: k.kind = Key::Kind::Function; k.name = "F13"; return k;
            case 26: k.kind = Key::Kind::Function; k.name = "F14"; return k;
            case 28: k.kind = Key::Kind::Function; k.name = "F15"; return k;
            case 29: k.kind = Key::Kind::Function; k.name = "F16"; return k;
            case 31: k.kind = Key::Kind::Function; k.name = "F17"; return k;
            case 32: k.kind = Key::Kind::Function; k.name = "F18"; return k;
            case 33: k.kind = Key::Kind::Function; k.name = "F19"; return k;
            case 34: k.kind = Key::Kind::Function; k.name = "F20"; return k;
            default: break;
            }
            break;
        }

        // Kitty keyboard protocol / XTerm extended: CSI u
        case 'u': {
            int cp = pi(0, 0);
            if (cp > 0) {
                k.codepoint = cp;
                // Build UTF-8 from codepoint for the name
                if (cp >= 0x20 && cp != 0x7f) {
                    k.kind = Key::Kind::Char;
                    // Encode codepoint to UTF-8
                    std::string utf8;
                    if (cp < 0x80)       utf8 += (char)cp;
                    else if (cp < 0x800) { utf8 += (char)(0xC0|(cp>>6)); utf8 += (char)(0x80|(cp&0x3F)); }
                    else                 { utf8 += (char)(0xE0|(cp>>12)); utf8 += (char)(0x80|((cp>>6)&0x3F)); utf8 += (char)(0x80|(cp&0x3F)); }
                    k.name = utf8;
                } else {
                    k.kind = Key::Kind::Control;
                    k.name = "Ctrl+" + std::string(1, (char)('a' + cp - 1));
                }
            }
            return k;
        }

        default: break;
        }

        k.kind = Key::Kind::Unknown;
        k.name = "ESC[" + body;
        return k;
    }

    // ── SS3 sequences  ESC O ... ─────────────────────────────────────────────
    if (c0 == 0x1b && raw.size() >= 3 && raw[1] == 'O') {
        switch (raw[2]) {
        case 'P': k.kind = Key::Kind::Function; k.name = "F1"; return k;
        case 'Q': k.kind = Key::Kind::Function; k.name = "F2"; return k;
        case 'R': k.kind = Key::Kind::Function; k.name = "F3"; return k;
        case 'S': k.kind = Key::Kind::Function; k.name = "F4"; return k;
        case 'A': k.kind = Key::Kind::Arrow;    k.name = "Up";    return k;
        case 'B': k.kind = Key::Kind::Arrow;    k.name = "Down";  return k;
        case 'C': k.kind = Key::Kind::Arrow;    k.name = "Right"; return k;
        case 'D': k.kind = Key::Kind::Arrow;    k.name = "Left";  return k;
        case 'H': k.kind = Key::Kind::Navigation; k.name = "Home"; return k;
        case 'F': k.kind = Key::Kind::Navigation; k.name = "End";  return k;
        default:  break;
        }
    }

    // ── Special single-byte control codes ────────────────────────────────────
    switch (c0) {
    case 0x00: k.kind = Key::Kind::Control; k.name = "Ctrl+Space"; k.mod_ctrl = true; return k;
    case 0x08: k.kind = Key::Kind::Backspace; k.name = "Backspace"; return k;
    case 0x09: k.kind = Key::Kind::Tab;       k.name = "Tab";       return k;
    case 0x0D: k.kind = Key::Kind::Enter;     k.name = "Enter";     return k;
    case 0x0A: k.kind = Key::Kind::Enter;     k.name = "Enter";     return k;
    case 0x1B: k.kind = Key::Kind::Escape;    k.name = "Escape";    return k;
    case 0x7F: k.kind = Key::Kind::Backspace; k.name = "Backspace"; return k;
    default: break;
    }

    // Ctrl+A..Z  (0x01-0x1A, excluding already handled ones)
    if (c0 >= 0x01 && c0 <= 0x1A) {
        k.kind      = Key::Kind::Control;
        k.mod_ctrl  = true;
        k.codepoint = c0;
        k.name      = "Ctrl+";
        k.name     += (char)('A' + c0 - 1);
        return k;
    }
    // Ctrl+[ \ ] ^ _  (0x1B-0x1F — 0x1B is ESC, handled above)
    if (c0 >= 0x1C && c0 <= 0x1F) {
        k.kind     = Key::Kind::Control;
        k.mod_ctrl = true;
        k.codepoint = c0;
        k.name     = "Ctrl+";
        k.name    += (char)('[' + (c0 - 0x1B));
        return k;
    }

    // ── Printable UTF-8 ───────────────────────────────────────────────────────
    k.kind      = Key::Kind::Char;
    k.codepoint = utf8_to_codepoint(raw);
    k.name      = raw; // the UTF-8 string itself is the name for printables
    return k;
}

// ─── getch / _getch ───────────────────────────────────────────────────────────

Key getch(int timeout_ms) {
    auto raw = read_key(timeout_ms);
    if (!raw) {
        Key k;
        k.kind = Key::Kind::Unknown;
        k.name = "";
        return k;
    }
    return parse_key(*raw);
}

std::optional<Key> _getch(int timeout_ms) {
    auto raw = read_key(timeout_ms);
    if (!raw) return std::nullopt;
    return parse_key(*raw);
}

// ─── Query cursor position (DSR) ──────────────────────────────────────────────

std::optional<Pos> query_cursor_pos() {
    // Send ESC[6n — CPR request
    std::fputs("\x1b[6n", stdout);
    fflush(stdout);

    // Expect ESC[row;colR
    std::string resp;
    for (int i = 0; i < 32; ++i) {
        int c = read_byte(200);
        if (c < 0) return std::nullopt;
        resp += (char)c;
        if (c == 'R') break;
    }

    // Parse ESC[row;colR
    if (resp.size() < 6) return std::nullopt;
    int row = 0, col = 0;
    if (std::sscanf(resp.c_str(), "\x1b[%d;%dR", &row, &col) == 2)
        return Pos{col - 1, row - 1};

    return std::nullopt;
}

} // namespace fbx::console 

