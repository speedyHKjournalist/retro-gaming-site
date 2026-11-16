/* Actual previous constructor from site203054a. Runtime negative control. */
static HRESULT WINAPI d3d_create_device(IDirect3D8* iface, UINT adapter,
    D3DDEVTYPE type, HWND focus_window, DWORD behavior,
    D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice8** device_out)
{
    D8Direct3D* d3d = d3d_from_iface(iface);
    D8Device* device;
    D9WGCreateDevice command;
    HWND window;
    RECT client;
    POINT origin;
    UINT width, height;
    UINT front_row_count;
    UINT front_byte_count;

    if (!device_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *device_out = NULL;
    if (adapter || type != D3DDEVTYPE_HAL || !parameters)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!d9wg_present_interval_valid(8, parameters->Windowed,
            parameters->FullScreen_PresentationInterval, D9WG_PRESENT_INTERVAL_MASK))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!d9wg_swap_effect_valid(8, parameters->SwapEffect, parameters->BackBufferCount))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!d9wg_present_multisample_valid(8, parameters->SwapEffect,
            parameters->MultiSampleType, 0, parameters->Flags))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!supported_multisample_type(parameters->MultiSampleType))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (parameters->EnableAutoDepthStencil
        && !supported_depth_stencil_format(
            parameters->AutoDepthStencilFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
        && !supported_backbuffer_format(parameters->BackBufferFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (parameters->BackBufferWidth > 8192
        || parameters->BackBufferHeight > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* D3D8 permits 0..3 back buffers (0 meaning 1). The host always presents
     * through a single WebGPU surface, so extra back buffers are a
     * presentation detail we can satisfy with one target; only a request
     * beyond the D3D8 maximum is a genuine error. */
    if (parameters->BackBufferCount > 3)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    window = parameters->hDeviceWindow ? parameters->hDeviceWindow
                                       : focus_window;
    SetRect(&client, 0, 0, 640, 480);
    origin.x = 0;
    origin.y = 0;
    if (window) {
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
    }
    width = d9wg_present_extent(parameters->BackBufferWidth,
        (uint32_t)(client.right - client.left), 640);
    height = d9wg_present_extent(parameters->BackBufferHeight,
        (uint32_t)(client.bottom - client.top), 480);
    if (width > 8192 || height > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    device = (D8Device*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*device));
    if (!device)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    d9wg_fpu_setup((behavior & D3DCREATE_FPU_PRESERVE) != 0);
    device->iface.lpVtbl = &g_device_vtbl;
    device->refcount = 1;
    device->parent = d3d;
    IDirect3D8_AddRef(iface);
    device->handle = allocate_handle();
    device->creation.AdapterOrdinal = adapter;
    device->creation.DeviceType = type;
    device->creation.hFocusWindow = focus_window;
    device->creation.BehaviorFlags = behavior;
    fill_display_mode(&device->display_mode,
        width, height,
        parameters->BackBufferFormat == D3DFMT_UNKNOWN
            ? D3DFMT_X8R8G8B8
            : parameters->BackBufferFormat);
    d9wg_present_parameters_effective(&device->present, parameters,
        width, height, device->display_mode.Format, window);
    device->viewport.X = 0;
    device->viewport.Y = 0;
    device->viewport.Width = device->display_mode.Width;
    device->viewport.Height = device->display_mode.Height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    device_init_states(device);
    device->depth_surface_enabled = parameters->EnableAutoDepthStencil;
    if (!texture_level_layout(device->display_mode.Format,
            device->display_mode.Width, device->display_mode.Height,
            &device->front_shadow_pitch, &front_row_count,
            &front_byte_count)
        || front_row_count != device->display_mode.Height) {
        IDirect3D8_Release(iface);
        HeapFree(GetProcessHeap(), 0, device);
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    }
    device->front_shadow = (BYTE*)HeapAlloc(GetProcessHeap(),
        HEAP_ZERO_MEMORY, front_byte_count);
    if (!device->front_shadow) {
        IDirect3D8_Release(iface);
        HeapFree(GetProcessHeap(), 0, device);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }

    command.device_handle = device->handle;
    command.hwnd = (uint32_t)(uintptr_t)window;
    command.x = origin.x;
    command.y = origin.y;
    command.width = width;
    command.height = height;
    command.backbuffer_format = device->display_mode.Format;
    command.windowed = parameters->Windowed;
    command.behavior_flags = behavior;
    command.enable_auto_depth_stencil = parameters->EnableAutoDepthStencil;
    command.auto_depth_stencil_format = parameters->AutoDepthStencilFormat;
    /* Validated above against supported_multisample_type(). Quality stays 0:
     * WebGPU has no quality levels, and the host refuses a non-zero one. */
    command.multisample_type = parameters->MultiSampleType;
    command.multisample_quality = 0;
    if (!emit_command(D9WG_OP_CREATE_DEVICE, &command, sizeof(command))) {
        IDirect3D8_Release(iface);
        d9wg_palette_free(&device->palettes);
        HeapFree(GetProcessHeap(), 0, device->front_shadow);
        HeapFree(GetProcessHeap(), 0, device);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    if (!emit_render_default_patches(device->handle, 8, device->present.EnableAutoDepthStencil, device->render_states)
        || !emit_object_default_patches(device->handle, &device->material)) {
        IDirect3DDevice8_Release(&device->iface);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }

    if (!reset_window_mode(device, window)) {
        IDirect3DDevice8_Release(&device->iface);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    attach_device_window(device, window);
    emit_surface_update_and_flush(device, window, FALSE, FALSE);

    d9wg_present_parameters_return(8, parameters, &device->present);
    *device_out = &device->iface;
    return D3D_OK;
}
