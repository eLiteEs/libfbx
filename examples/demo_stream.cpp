/**
 * demo_stream.cpp
 *
 * Demonstrates fbxostream (write to framebuffer like cout)
 * and fbxistream (readline inside the framebuffer).
 */

#include <fbx/fbx.hpp>
#include <iostream>

int main() {
    using namespace fbx;

    console::enter_alt_screen();
    console::hide_cursor();
    console::set_raw_mode(true);

    Framebuffer fb(console::get_size());

    fbxostream out(fb);
    fbxistream in(fb);

    // Write some styled content via the stream interface
    out.at(0, 0);
    out << "\x1b[1;36m" << "fbx stream demo\x1b[0m\n";
    out << "\x1b[2;37m" << "Type something and press Enter:\x1b[0m\n";

    fb.render();
    console::show_cursor();

    // Read a line interactively inside the framebuffer
    std::string name = in.readline("  > ");

    console::hide_cursor();

    out.at(0, 5);
    out << "\x1b[1;32m" << "You typed: " << "\x1b[0;97m" << name << "\x1b[0m\n";
    out << "\x1b[2;37m" << "(Press any key to exit)" << "\x1b[0m";

    fb.render();
    console::read_key(-1);

    console::set_raw_mode(false);
    console::show_cursor();
    console::leave_alt_screen();

    // Back to normal stdout
    std::cout << "You typed: " << name << "\n";
    return 0;
}

