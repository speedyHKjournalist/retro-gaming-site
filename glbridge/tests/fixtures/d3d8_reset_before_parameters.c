/* Actual previous implementation from site20ab5b5. Runtime negative control. */
static HRESULT WINAPI device_reset(IDirect3DDevice8* iface,
    D3DPRESENT_PARAMETERS* parameters)
{
    D8Device* device = device_from_iface(iface);
    D9WGResetDevice reset;
    RECT client;
    POINT origin;
    HWND window;
    BYTE* new_front_shadow;
    UINT new_front_pitch;
    UINT new_front_rows;
    UINT new_front_bytes;

    d9wg_reset_begin(&device->reset_state);
    if (!parameters || !supported_multisample_type(parameters->MultiSampleType)
        || parameters->BackBufferCount > 3)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!d9wg_present_interval_valid(8, parameters->Windowed,
            parameters->FullScreen_PresentationInterval, D9WG_PRESENT_INTERVAL_MASK))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!d9wg_swap_effect_valid(8, parameters->SwapEffect, parameters->BackBufferCount))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!d9wg_present_multisample_valid(8, parameters->SwapEffect,
            parameters->MultiSampleType, 0, parameters->Flags))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (parameters->EnableAutoDepthStencil
        && !supported_depth_stencil_format(
            parameters->AutoDepthStencilFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
        && !supported_backbuffer_format(parameters->BackBufferFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    window = parameters->hDeviceWindow ? parameters->hDeviceWindow
                                       : device->creation.hFocusWindow;
    SetRect(&client, 0, 0, 640, 480);
    origin.x = origin.y = 0;
    if (window) {
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
    }
    ZeroMemory(&reset, sizeof(reset));
    reset.old_device_handle = device->handle;
    reset.new_device_handle = allocate_handle();
    reset.hwnd = (uint32_t)(uintptr_t)window;
    reset.x = origin.x;
    reset.y = origin.y;
    reset.width = d9wg_present_extent(parameters->BackBufferWidth,
        (uint32_t)(client.right - client.left), device->display_mode.Width);
    reset.height = d9wg_present_extent(parameters->BackBufferHeight,
        (uint32_t)(client.bottom - client.top), device->display_mode.Height);
    reset.backbuffer_format = parameters->BackBufferFormat == D3DFMT_UNKNOWN
        ? device->display_mode.Format
        : parameters->BackBufferFormat;
    reset.windowed = parameters->Windowed;
    reset.behavior_flags = device->creation.BehaviorFlags;
    reset.enable_auto_depth_stencil = parameters->EnableAutoDepthStencil;
    reset.auto_depth_stencil_format = parameters->AutoDepthStencilFormat;
    /* Validated above against supported_multisample_type(). */
    reset.multisample_type = parameters->MultiSampleType;
    reset.multisample_quality = 0;
    if (reset.width > 8192 || reset.height > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!texture_level_layout((D3DFORMAT)reset.backbuffer_format,
            reset.width, reset.height, &new_front_pitch, &new_front_rows,
            &new_front_bytes)
        || new_front_rows != reset.height)
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    device_cancel_recording(device);
    if (device_has_reset_blockers(device))
        return D8WG_TRACE_ERROR(D3DERR_DEVICELOST);
    new_front_shadow = (BYTE*)HeapAlloc(GetProcessHeap(),
        HEAP_ZERO_MEMORY, new_front_bytes);
    if (!new_front_shadow)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    if (!emit_command(D9WG_OP_RESET, &reset, sizeof(reset))) {
        HeapFree(GetProcessHeap(), 0, new_front_shadow);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    /* Reset ends an active Scene once the host accepts the new device. */
    device->in_scene = FALSE;
    device->handle = reset.new_device_handle;
    device_clear_bindings(device);
    HeapFree(GetProcessHeap(), 0, device->front_shadow);
    device->front_shadow = new_front_shadow;
    device->front_shadow_pitch = new_front_pitch;
    ++device->reset_epoch;
    d9wg_present_parameters_effective(&device->present, parameters,
        reset.width, reset.height, reset.backbuffer_format, window);
    fill_display_mode(&device->display_mode, reset.width, reset.height,
        reset.backbuffer_format);
    if (device->implicit_back_buffer) {
        D8Surface* surface = device->implicit_back_buffer;
        surface->desc.Width = reset.width;
        surface->desc.Height = reset.height;
        surface->desc.Format = reset.backbuffer_format;
        surface->desc.MultiSampleType = parameters->MultiSampleType;
        surface->desc.Size = new_front_bytes;
        surface->row_pitch = new_front_pitch;
    }
    if (device->implicit_depth_stencil) {
        D8Surface* surface = device->implicit_depth_stencil;
        surface->desc.Width = reset.width;
        surface->desc.Height = reset.height;
        surface->desc.Format = parameters->AutoDepthStencilFormat;
        surface->desc.MultiSampleType = parameters->MultiSampleType;
        surface->row_pitch = reset.width * 4u;
        surface->desc.Size = surface->row_pitch * reset.height;
    }
    device_init_states(device);
    device->viewport.X = device->viewport.Y = 0;
    device->viewport.Width = reset.width;
    device->viewport.Height = reset.height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    device->depth_surface_enabled = parameters->EnableAutoDepthStencil;
    if (!emit_device_state_after_reset(device))
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    if (!reset_window_mode(device, window))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (window != device->tracked_window) {
        detach_device_window(device);
        attach_device_window(device, window);
    }
    emit_surface_update_and_flush(device, window, FALSE, FALSE);
    d9wg_present_parameters_return(8, parameters, &device->present);
    d9wg_reset_complete(&device->reset_state);
    return D3D_OK;
}
