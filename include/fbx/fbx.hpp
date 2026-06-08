#pragma once

/**
 * fbx — Console Framebuffer Library
 *
 * Cross-platform (Linux/Windows) terminal framebuffer with:
 *   - Unicode + ANSI cell model
 *   - Region save/restore (Framebuffer snapshots)
 *   - Multi-framebuffer manager with layer compositing and tiling
 *   - std::ostream-compatible stream interface
 *   - Escape sequence parsing for cursor/color control
 */

#include "framebuffer.hpp"
#include "cell.hpp"
#include "console.hpp"
#include "manager.hpp"
#include "stream.hpp"

