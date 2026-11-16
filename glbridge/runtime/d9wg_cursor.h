#ifndef D9WG_CURSOR_H
#define D9WG_CURSOR_H
#include <stdint.h>

typedef struct D9WGCursorState {
    int ready, visible, hardware;
    int32_t screen_x, screen_y;
} D9WGCursorState;

typedef struct D9WGCursorImage {
    uint32_t device, handle, level, face, format, width, height;
    uint32_t pitch, byte_count, adapter_width, adapter_height;
    const uint8_t* pixels;
    int readback, locked;
} D9WGCursorImage;

#endif
