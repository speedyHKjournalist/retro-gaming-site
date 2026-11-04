/* Actual ordinary9 Reset,site4ed9b4d. */
static HRESULT WINAPI device_reset(IDirect3DDevice9* iface,
    D3DPRESENT_PARAMETERS* parameters)
{
    D9Device* device = device_from_iface(iface);
    D9WGResetDevice reset;
    RECT client;
    POINT origin;
    HWND window;
    D3DFORMAT backbuffer_format;

    d9wg_reset_begin(&device->reset_state);
    /* Reset was completely untraced, which left a hole exactly where one is
     * least affordable: a title that resets to apply its saved video settings
     * does so once, early, and everything after depends on it having worked.
     * Its three refusal paths returned bare HRESULTs, indistinguishable in a
     * trace from the call never happening. */
    TRACE("CALL Reset %lux%lu fmt=%08lX count=%lu multisample=%lu windowed=%lu "
          "auto_depth=%lu depth_fmt=%08lX interval=%08lX hwnd=%08lX",
        parameters ? parameters->BackBufferWidth : 0,
        parameters ? parameters->BackBufferHeight : 0,
        parameters ? (DWORD)parameters->BackBufferFormat : 0,
        parameters ? parameters->BackBufferCount : 0,
        parameters ? (DWORD)parameters->MultiSampleType : 0,
        parameters ? (DWORD)parameters->Windowed : 0,
        parameters ? (DWORD)parameters->EnableAutoDepthStencil : 0,
        parameters ? (DWORD)parameters->AutoDepthStencilFormat : 0,
        parameters ? parameters->PresentationInterval : 0,
        parameters ? (DWORD)(uintptr_t)parameters->hDeviceWindow : 0);
    /* D3D9 takes up to D3DPRESENT_BACK_BUFFERS_MAX (3) back buffers here,
     * as CreateDevice does; refusing more than one failed every Reset of a
     * double- or triple-buffered game (review S1-9b) */
    if (!parameters || parameters->BackBufferCount > 3) {
        TRACE("FAIL Reset parameters=%lu multisample=%lu count=%lu "
              "blockers=%lu -> %08lX",
            (DWORD)(parameters != NULL),
            parameters ? (DWORD)parameters->MultiSampleType : 0,
            parameters ? parameters->BackBufferCount : 0,
            (DWORD)device_has_reset_blockers(device),
            (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!d9wg_present_interval_valid(9, parameters->Windowed,
            parameters->PresentationInterval, D9WG_PRESENT_INTERVAL_MASK))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_swap_effect_valid(9, parameters->SwapEffect, parameters->BackBufferCount))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_present_multisample_valid(9, parameters->SwapEffect,
            parameters->MultiSampleType, parameters->MultiSampleQuality, parameters->Flags))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!parameters->Windowed && parameters->BackBufferFormat == D3DFMT_UNKNOWN)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    backbuffer_format = (D3DFORMAT)d9wg_display_mode_backbuffer_format(
        parameters->BackBufferFormat);
    if (!supported_multisample(backbuffer_format, parameters->MultiSampleType)) {
        TRACE("FAIL Reset unsupported multisample=%lu quality=%lu -> %08lX",
            (DWORD)parameters->MultiSampleType,
            parameters->MultiSampleQuality,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
        && !supported_depth_stencil_format(
            parameters->AutoDepthStencilFormat)) {
        TRACE("FAIL Reset depth_fmt=%08lX -> %08lX",
            (DWORD)parameters->AutoDepthStencilFormat,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
        && !supported_multisample(parameters->AutoDepthStencilFormat,
            parameters->MultiSampleType))
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
        && !supported_backbuffer_format(parameters->BackBufferFormat)) {
        TRACE("FAIL Reset backbuffer_fmt=%08lX -> %08lX",
            (DWORD)parameters->BackBufferFormat,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
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
    reset.backbuffer_format = backbuffer_format;
    reset.windowed = parameters->Windowed;
    reset.behavior_flags = device->creation.BehaviorFlags;
    reset.enable_auto_depth_stencil = parameters->EnableAutoDepthStencil;
    reset.auto_depth_stencil_format = parameters->AutoDepthStencilFormat;
    reset.multisample_type = parameters->MultiSampleType;
    reset.multisample_quality = parameters->MultiSampleQuality;
    if (reset.width > 8192 || reset.height > 8192)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    device_cancel_recording(device);
    if (device_has_reset_blockers(device))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!emit_command(D9WG_OP_RESET, &reset, sizeof(reset)))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    /* Reset ends an active Scene once the host accepts the new device. */
    device->in_scene = FALSE;
    device->handle = reset.new_device_handle;
    device_clear_bindings(device);
    ++device->reset_epoch;
    d9wg_present_parameters_effective(&device->present, parameters,
        reset.width, reset.height, reset.backbuffer_format, window);
    fill_display_mode(&device->display_mode, reset.width, reset.height,
        reset.backbuffer_format);
    /* A Reset resizes the implicit surfaces rather than replacing them -- the
     * app keeps the same pointers across it -- so the cached objects have to
     * pick up the new geometry here or GetDesc keeps reporting the old size. */
    if (device->implicit_back_buffer) {
        device->implicit_back_buffer->width = device->display_mode.Width;
        device->implicit_back_buffer->height = device->display_mode.Height;
        device->implicit_back_buffer->format = device->display_mode.Format;
        device->implicit_back_buffer->multisample = parameters->MultiSampleType;
        device->implicit_back_buffer->multisample_quality = parameters->MultiSampleQuality;
    }
    if (device->implicit_depth_stencil) {
        device->implicit_depth_stencil->width = reset.width;
        device->implicit_depth_stencil->height = reset.height;
        device->implicit_depth_stencil->format = parameters->AutoDepthStencilFormat;
        device->implicit_depth_stencil->multisample = parameters->MultiSampleType;
        device->implicit_depth_stencil->multisample_quality = parameters->MultiSampleQuality;
    }
    device_init_states(device);
    d9wg_window_cache_invalidate(&device->window_cache);
    device->viewport.X = device->viewport.Y = 0;
    device->viewport.Width = reset.width;
    device->viewport.Height = reset.height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    SetRect(&device->scissor_rect, 0, 0, (int)reset.width, (int)reset.height);
    if (!emit_device_state_after_reset(device)) {
        TRACE("FAIL Reset emit_device_state_after_reset -> %08lX",
            (DWORD)D3DERR_DRIVERINTERNALERROR);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    if (!reset_window_mode(device, window))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    emit_window_state(device, window);
    TRACE("OK Reset old_handle=%08lX new_handle=%08lX %lux%lu epoch=%lu",
        reset.old_device_handle, device->handle, reset.width, reset.height,
        device->reset_epoch);

    d9wg_present_parameters_return(9, parameters, &device->present);
    d9wg_reset_complete(&device->reset_state);
    return D3D_OK;
}
