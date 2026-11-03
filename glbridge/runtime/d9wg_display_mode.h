#ifndef D9WG_DISPLAY_MODE_H
#define D9WG_DISPLAY_MODE_H
#include <stdint.h>

typedef struct D9WGDisplayMode {
    uint32_t width, height, format, refresh_rate;
} D9WGDisplayMode;

/* Keep the existing D3D9 fallback/unknown-refresh convention while sharing the
 * actual adapter query. Backbuffer dimensions belong to resource descriptors. */
static inline int d9wg_display_mode_current(D9WGDisplayMode* mode)
{
    DEVMODEA current;
    ZeroMemory(&current, sizeof(current));
    current.dmSize = sizeof(current);
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &current)
        && current.dmPelsWidth && current.dmPelsHeight) {
        mode->width = current.dmPelsWidth;
        mode->height = current.dmPelsHeight;
        mode->format = current.dmBitsPerPel == 16 ? 23u : 22u;
        mode->refresh_rate = current.dmDisplayFrequency > 1 ? current.dmDisplayFrequency : 60u;
        return 1;
    }
    *mode = (D9WGDisplayMode) { 1024, 768, 22, 60 };
    return 0;
}
#endif
