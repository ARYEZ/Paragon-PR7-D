#include "ui/DawKeys.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace dawkeys
{
    void postSpaceToParent (void* nativeHandle)
    {
#if defined (_WIN32)
        // As JUCE's own forwardMessageToParent does with a key nobody in the plugin used.
        if (nativeHandle == nullptr)
            return;
        if (HWND parent = GetParent ((HWND) nativeHandle))
        {
            const auto scan = (LPARAM) MapVirtualKeyW (VK_SPACE, 0 /* MAPVK_VK_TO_VSC */);
            PostMessageW (parent, WM_KEYDOWN, VK_SPACE, 1 | (scan << 16));
            PostMessageW (parent, WM_KEYUP, VK_SPACE, (LPARAM) (1 | (scan << 16) | (1LL << 30) | (1LL << 31)));
        }
#else
        (void) nativeHandle;
#endif
    }
}
