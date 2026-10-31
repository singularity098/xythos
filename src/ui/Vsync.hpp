// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#pragma once

#include <cstdint>

namespace xythos {

// Turns off vsync for the window whose OpenGL context is current (call it while drawing). Plugin UIs draw from the
// host's GUI thread, so a buffer swap that waits for the screen's refresh stalls the host's idle loop, which on
// Linux is also what delivers our mouse events: dragging gets choppy. `done` is the drawable it was last applied to,
// so calling it every frame only does the work once per window. Linux (GLX) only; a no-op elsewhere.
void disableVsync(uintptr_t& done);

// Gives the window whose OpenGL context is current its own arrow cursor (call it while drawing). An X11 window without
// a cursor shows its parent's: in Reaper, the FX window's resize cursor stayed on over the whole plugin after the
// pointer crossed the window's border. pugl doesn't define one (its puglSetCursor() skips the arrow it starts with).
// `done` is the drawable it was last applied to, as for disableVsync(). Linux (X11) only; a no-op elsewhere.
void defineArrowCursor(uintptr_t& done);

} // namespace xythos
