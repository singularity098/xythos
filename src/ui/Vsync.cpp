// SPDX-License-Identifier: MIT
// Copyright (c) 2026 singularity098

#include "Vsync.hpp"

#if defined(__linux__)
#include <GL/glx.h>
#include <X11/cursorfont.h>
#include <cstring>
#endif

namespace xythos {

void disableVsync(uintptr_t& done)
{
#if defined(__linux__)
    Display* const display = glXGetCurrentDisplay();
    const GLXDrawable drawable = glXGetCurrentDrawable();
    if (display == nullptr || drawable == 0 || static_cast<uintptr_t>(drawable) == done)
        return;
    done = static_cast<uintptr_t>(drawable);

    const char* const extensions = glXQueryExtensionsString(display, DefaultScreen(display));
    if (extensions == nullptr)
        return;
    using SwapIntervalExt = void (*)(Display*, GLXDrawable, int);
    using SwapIntervalMesa = int (*)(unsigned int);
    if (std::strstr(extensions, "GLX_EXT_swap_control") != nullptr)
    {
        if (auto swapInterval = reinterpret_cast<SwapIntervalExt>(
                glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXSwapIntervalEXT"))))
            swapInterval(display, drawable, 0);
    }
    else if (std::strstr(extensions, "GLX_MESA_swap_control") != nullptr)
    {
        if (auto swapInterval = reinterpret_cast<SwapIntervalMesa>(
                glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXSwapIntervalMESA"))))
            swapInterval(0);
    }
#else
    (void)done;
#endif
}

void defineArrowCursor(uintptr_t& done)
{
#if defined(__linux__)
    Display* const display = glXGetCurrentDisplay();
    const GLXDrawable drawable = glXGetCurrentDrawable();
    if (display == nullptr || drawable == 0 || static_cast<uintptr_t>(drawable) == done)
        return;
    done = static_cast<uintptr_t>(drawable);
    // the theme's arrow (libX11 loads the cursor theme's image when Xcursor is installed)
    const Cursor cursor = XCreateFontCursor(display, XC_left_ptr);
    if (cursor == None)
        return;
    XDefineCursor(display, static_cast<Window>(drawable), cursor);
    XFreeCursor(display, cursor);
    XFlush(display);
#else
    (void)done;
#endif
}

} // namespace xythos
