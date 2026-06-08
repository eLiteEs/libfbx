/**
 * demo_layers.cpp
 *
 * Demonstrates:
 *  - FramebufferManager with two layers
 *  - Layer compositing (overlay a "dialog" over a background)
 *  - Region snapshot (save/restore area behind the dialog)
 *  - fbxostream usage
 */

#include <fbx/fbx.hpp>
#include <thread>
#include <chrono>
#include <cstdio>

int main() {
    using namespace fbx;

    console::enter_alt_screen();
    console::hide_cursor();
    console::set_raw_mode(true);

    FramebufferManager mgr;

    // Background layer — fills the whole terminal
    FbId bg_id = mgr.create_fullscreen("background");
    FbId dlg_id = mgr.create(40, 10, "dialog");

    mgr.add_layer(bg_id,  0, 0, 0);  // z=0 → bottom
    mgr.add_layer(dlg_id, 5, 3, 1);  // z=1 → on top

    Framebuffer* bg  = mgr.get(bg_id);
    Framebuffer* dlg = mgr.get(dlg_id);

    // ── Draw background ───────────────────────────────────────────────────────
    {
        fbxostream out(*bg);
        Size sz = console::get_size();
        for (int r = 0; r < sz.rows; ++r) {
            for (int c = 0; c < sz.cols; ++c) {
                // Checkerboard of two shades
                bool alt = (r + c) % 2 == 0;
                bg->put_char(c, r, alt ? "░" : "▒",
                             Color::palette(alt ? 8 : 6));
            }
        }
        out.at(2, 1);
        out << "\x1b[1;33m" << "fbx demo — background layer" << "\x1b[0m";
    }

    // ── Save region behind dialog ─────────────────────────────────────────────
    Snapshot behind = bg->save_region({5, 3, 40, 10});

    // ── Draw dialog ───────────────────────────────────────────────────────────
    {
        fbxostream out(*dlg);

        // Fill dialog background
        dlg->fill_rect({0, 0, 40, 10},
                        Cell{" ", Color::none(), Color::palette(4)});

        // Border
        dlg->put_char(0,  0,  "╔", Color::white(), Color::palette(4));
        dlg->put_char(39, 0,  "╗", Color::white(), Color::palette(4));
        dlg->put_char(0,  9,  "╚", Color::white(), Color::palette(4));
        dlg->put_char(39, 9,  "╝", Color::white(), Color::palette(4));
        for (int c = 1; c < 39; ++c) {
            dlg->put_char(c, 0, "═", Color::white(), Color::palette(4));
            dlg->put_char(c, 9, "═", Color::white(), Color::palette(4));
        }
        for (int r = 1; r < 9; ++r) {
            dlg->put_char(0,  r, "║", Color::white(), Color::palette(4));
            dlg->put_char(39, r, "║", Color::white(), Color::palette(4));
        }

        // Title
        out.at(2, 0);
        out << "\x1b[1;97m" << " fbx::Dialog " << "\x1b[0m";

        // Content
        out.at(3, 2);
        out << "\x1b[0;97m" << "Layer compositing demo" << "\x1b[0m";
        out.at(3, 4);
        out << "\x1b[0;93m" << "This dialog is on z=1" << "\x1b[0m";
        out.at(3, 6);
        out << "\x1b[2;37m" << "Press any key to dismiss..." << "\x1b[0m";
    }

    // ── Flush + wait ──────────────────────────────────────────────────────────
    mgr.flush();
    console::read_key(-1); // wait for any key

    // ── Dismiss dialog: restore background region ─────────────────────────────
    bg->restore_region(behind);
    mgr.remove_layer(dlg_id);

    bg->set_cursor(2, 3);
    bg->print("\x1b[1;32mDialog dismissed — region restored!\x1b[0m");

    mgr.flush();
    console::read_key(-1);

    // ── Cleanup ───────────────────────────────────────────────────────────────
    console::set_raw_mode(false);
    console::show_cursor();
    console::leave_alt_screen();
    return 0;
}

