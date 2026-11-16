#ifndef D9WG_CURSOR_BRIDGE_H
#define D9WG_CURSOR_BRIDGE_H
#include "d9wg_cursor.h"

static BOOL d9wg_cursor_image_valid(const D9WGCursorImage* image)
{
    return image->format == D3DFMT_A8R8G8B8 && !image->locked
        && image->width && image->height
        && !(image->width & (image->width - 1))
        && !(image->height & (image->height - 1))
        && image->width <= image->adapter_width
        && image->height <= image->adapter_height
        && image->width <= UINT32_MAX / 4u
        && image->height <= UINT32_MAX / (image->width * 4u);
}

static HRESULT d9wg_cursor_upload(uint32_t device_handle, UINT hotspot_x,
    UINT hotspot_y, const D9WGCursorImage* image)
{
    if (!d9wg_cursor_image_valid(image))
        return D3DERR_INVALIDCALL;
    uint32_t row_bytes = image->width * 4u;
    uint32_t byte_count = row_bytes * image->height;
    uint32_t pitch = image->pitch;
    const BYTE* pixels = image->pixels;
    BYTE* temporary = NULL;
    if (image->readback) {
        D9WGReadbackSurface request;
        D9WGReadbackDiagnostic detail;
        temporary = HeapAlloc(GetProcessHeap(), 0, byte_count);
        if (!temporary)
            return E_OUTOFMEMORY;
        ZeroMemory(&request, sizeof(request));
        request.device_handle = image->device;
        request.texture_handle = image->handle;
        request.level = image->level;
        request.face = image->face;
        request.format = image->format;
        request.width = image->width;
        request.height = image->height;
        request.destination_pitch = row_bytes;
        int result = d9wg_surface_readback(g_process, &request, row_bytes,
            image->height, temporary, byte_count, submit_readback_batch_locked, &detail);
        if (result) {
            HeapFree(GetProcessHeap(), 0, temporary);
            return result == D9WG_READBACK_INVALID ? D3DERR_INVALIDCALL : D3DERR_DRIVERINTERNALERROR;
        }
        pixels = temporary;
        pitch = row_bytes;
    } else if (!pixels || pitch < row_bytes || image->byte_count < row_bytes
        || image->height - 1u > (image->byte_count - row_bytes) / pitch)
        return D3DERR_INVALIDCALL;

    D9WGSetCursorProperties command;
    uint8_t *payload, *blob;
    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    BOOL result = reserve_command_locked(D9WG_OP_SET_CURSOR_PROPERTIES,
        sizeof(command), byte_count, NULL, &payload, &blob);
    if (result) {
        command.device_handle = device_handle;
        command.hotspot_x = hotspot_x;
        command.hotspot_y = hotspot_y;
        command.width = image->width;
        command.height = image->height;
        command.data_bytes = byte_count;
        command.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        for (UINT row = 0; row < image->height; ++row)
            CopyMemory(blob + row * row_bytes, pixels + row * pitch, row_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    if (temporary)
        HeapFree(GetProcessHeap(), 0, temporary);
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static BOOL d9wg_cursor_visibility(uint32_t device_handle, BOOL visible)
{
    D9WGShowCursor command;
    command.device_handle = device_handle;
    command.show = visible ? 1u : 0u;
    return emit_command(D9WG_OP_SHOW_CURSOR, &command, sizeof(command));
}

static BOOL d9wg_cursor_commit(uint32_t device_handle, D9WGCursorState* cursor,
    UINT width, UINT height)
{
    BOOL first = !cursor->ready;
    cursor->ready = TRUE;
    cursor->hardware = width == 32 && height == 32;
    if (first) {
        cursor->visible = FALSE;
        /* An app image takes over from the optional D3D9 GDI fallback. */
        return d9wg_cursor_visibility(device_handle, FALSE);
    }
    return TRUE;
}

static void d9wg_cursor_position_wire(uint32_t device_handle, int32_t x,
    int32_t y, DWORD flags)
{
    D9WGSetCursorPosition command;
    command.device_handle = device_handle;
    command.x = x;
    command.y = y;
    command.flags = flags;
    emit_command(D9WG_OP_SET_CURSOR_POSITION, &command, sizeof(command));
}

static void d9wg_cursor_client_position(uint32_t device_handle, HWND window,
    const D9WGCursorState* cursor, DWORD flags)
{
    POINT point = { cursor->screen_x, cursor->screen_y };
    if (window)
        ScreenToClient(window, &point);
    d9wg_cursor_position_wire(device_handle, point.x, point.y, flags);
}

static void d9wg_cursor_set_position(uint32_t device_handle, HWND window,
    D9WGCursorState* cursor, int32_t x, int32_t y, DWORD flags)
{
    cursor->screen_x = x;
    cursor->screen_y = y;
    if (cursor->ready && cursor->hardware) {
        POINT point;
        if (!GetCursorPos(&point) || point.x != x || point.y != y)
            SetCursorPos(x, y);
        if (!GetCursorPos(&point) || point.x != x || point.y != y)
            cursor->hardware = FALSE;
    }
    d9wg_cursor_client_position(device_handle, window, cursor, flags);
}

static WINBOOL d9wg_cursor_show(uint32_t device_handle, HWND window,
    D9WGCursorState* cursor, WINBOOL show)
{
    if (!cursor->ready)
        return FALSE;
    WINBOOL previous = cursor->visible;
    if (show && !previous) {
        POINT point;
        if (GetCursorPos(&point)) {
            cursor->screen_x = point.x;
            cursor->screen_y = point.y;
        }
        d9wg_cursor_client_position(device_handle, window, cursor, 0);
    }
    cursor->visible = !!show;
    d9wg_cursor_visibility(device_handle, cursor->visible);
    return previous;
}

static void d9wg_cursor_present(uint32_t device_handle, HWND window,
    D9WGCursorState* cursor)
{
    if (!cursor->ready || !cursor->visible)
        return;
    if (cursor->hardware) {
        POINT point;
        if (GetCursorPos(&point)) {
            cursor->screen_x = point.x;
            cursor->screen_y = point.y;
        }
    }
    d9wg_cursor_client_position(device_handle, window, cursor, 0);
}

#endif
