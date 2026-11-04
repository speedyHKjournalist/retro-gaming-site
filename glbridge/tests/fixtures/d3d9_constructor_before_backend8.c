/* Actual prior9 constructor from site2d3188c;positive ordinary9 oracle. */
static HRESULT WINAPI d3d_create_device(IDirect3D9* iface, UINT adapter,
    D3DDEVTYPE type, HWND focus_window, DWORD behavior,
    D3DPRESENT_PARAMETERS* parameters,
    IDirect3DDevice9** device_out)
{
    D9Direct3D* d3d = d3d_from_iface(iface);
    D9Device* device;
    D9WGCreateDevice command;
    HWND window;
    RECT client;
    POINT origin;
    UINT width, height;
    D3DFORMAT backbuffer_format;

    TRACE("CALL CreateDevice adapter=%lu type=%lu focus=%08lX behavior=%08lX",
        adapter, (DWORD)type, (DWORD)(uintptr_t)focus_window, behavior);

    if (!device_out) {
        TRACE("FAIL CreateDevice missing output -> %08lX",
            (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    *device_out = NULL;
    if (adapter || type != D3DDEVTYPE_HAL || !parameters) {
        TRACE("FAIL CreateDevice invalid arguments -> %08lX",
            (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    TRACE("CreateDevice params %lux%lu fmt=%08lX count=%lu multisample=%lu "
          "quality=%lu swap=%lu windowed=%lu auto_depth=%lu "
          "depth_fmt=%08lX flags=%08lX interval=%08lX hwnd=%08lX",
        parameters->BackBufferWidth, parameters->BackBufferHeight,
        (DWORD)parameters->BackBufferFormat, parameters->BackBufferCount,
        (DWORD)parameters->MultiSampleType, parameters->MultiSampleQuality,
        (DWORD)parameters->SwapEffect, (DWORD)parameters->Windowed,
        (DWORD)parameters->EnableAutoDepthStencil,
        (DWORD)parameters->AutoDepthStencilFormat, parameters->Flags,
        parameters->PresentationInterval,
        (DWORD)(uintptr_t)parameters->hDeviceWindow);
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
    if (!supported_multisample(backbuffer_format,
            parameters->MultiSampleType)) {
        TRACE("FAIL CreateDevice unsupported multisample=%lu quality=%lu "
              "format=%08lX -> %08lX",
            (DWORD)parameters->MultiSampleType,
            parameters->MultiSampleQuality, (DWORD)backbuffer_format,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
        && !supported_depth_stencil_format(
            parameters->AutoDepthStencilFormat)) {
        TRACE("FAIL CreateDevice unsupported depth_fmt=%08lX -> %08lX",
            (DWORD)parameters->AutoDepthStencilFormat,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
        && !supported_multisample(parameters->AutoDepthStencilFormat,
            parameters->MultiSampleType)) {
        TRACE("FAIL CreateDevice depth multisample mismatch depth_fmt=%08lX "
              "multisample=%lu -> %08lX",
            (DWORD)parameters->AutoDepthStencilFormat,
            (DWORD)parameters->MultiSampleType,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
        && !supported_backbuffer_format(parameters->BackBufferFormat)) {
        TRACE("FAIL CreateDevice unsupported backbuffer_fmt=%08lX -> %08lX",
            (DWORD)parameters->BackBufferFormat,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->BackBufferWidth > 8192
        || parameters->BackBufferHeight > 8192) {
        TRACE("FAIL CreateDevice oversized backbuffer=%lux%lu -> %08lX",
            parameters->BackBufferWidth, parameters->BackBufferHeight,
            (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (parameters->BackBufferCount > 3) {
        TRACE("FAIL CreateDevice backbuffer_count=%lu -> %08lX",
            parameters->BackBufferCount, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    /*
     * BehaviorFlags was recorded and otherwise ignored, which meant an app
     * could ask for a combination D3D9 rejects and be handed a device anyway --
     * and then behave as if the combination it asked for were in force. D3D9
     * requires exactly one vertex-processing mode, and D3DCREATE_PUREDEVICE
     * only alongside the hardware one.
     */
    {
        const DWORD vertex_processing = behavior
            & (D3DCREATE_SOFTWARE_VERTEXPROCESSING
                | D3DCREATE_HARDWARE_VERTEXPROCESSING
                | D3DCREATE_MIXED_VERTEXPROCESSING);
        if (vertex_processing != D3DCREATE_SOFTWARE_VERTEXPROCESSING
            && vertex_processing != D3DCREATE_HARDWARE_VERTEXPROCESSING
            && vertex_processing != D3DCREATE_MIXED_VERTEXPROCESSING) {
            TRACE("FAIL CreateDevice behavior=%08lX names %s vertex processing "
                  "mode -> %08lX",
                behavior,
                vertex_processing ? "more than one" : "no",
                (DWORD)D3DERR_INVALIDCALL);
            HOSTLOG_FAILED("CreateDevice refused: BehaviorFlags %08lX must "
                           "name exactly one vertex-processing mode",
                behavior);
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
        if ((behavior & D3DCREATE_PUREDEVICE)
            && vertex_processing != D3DCREATE_HARDWARE_VERTEXPROCESSING) {
            TRACE("FAIL CreateDevice behavior=%08lX pairs PUREDEVICE with "
                  "non-hardware vertex processing -> %08lX",
                behavior,
                (DWORD)D3DERR_INVALIDCALL);
            HOSTLOG_FAILED("CreateDevice refused: D3DCREATE_PUREDEVICE "
                           "requires D3DCREATE_HARDWARE_VERTEXPROCESSING");
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
    }

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
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    device = (D9Device*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*device));
    if (!device) {
        TRACE("FAIL CreateDevice allocation -> %08lX", (DWORD)E_OUTOFMEMORY);
        return E_OUTOFMEMORY;
    }
    d9wg_fpu_setup((behavior & D3DCREATE_FPU_PRESERVE) != 0);
    device->iface.lpVtbl = &g_device_vtbl;
    device->implicit_swap_chain.iface.lpVtbl = &g_swap_chain_vtbl;
    device->implicit_swap_chain.device = device;
    device->refcount = 1;
    device->parent = d3d;
    IDirect3D9_AddRef(iface);
    device->handle = allocate_handle();
    device->creation.AdapterOrdinal = adapter;
    device->creation.DeviceType = type;
    device->creation.hFocusWindow = focus_window;
    device->creation.BehaviorFlags = behavior;
    fill_display_mode(&device->display_mode,
        width, height,
        backbuffer_format);
    d9wg_present_parameters_effective(&device->present, parameters,
        width, height, device->display_mode.Format, window);
    device->viewport.X = 0;
    device->viewport.Y = 0;
    device->viewport.Width = device->display_mode.Width;
    device->viewport.Height = device->display_mode.Height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    /* D3D9's default scissor rect is the whole back buffer (a state block of
     * D3DSBT_ALL captured it as {0,0,0,0}) */
    SetRect(&device->scissor_rect, 0, 0, (int)device->display_mode.Width,
        (int)device->display_mode.Height);
    device_init_states(device);

    ZeroMemory(&command, sizeof(command));
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
    command.multisample_type = parameters->MultiSampleType;
    command.multisample_quality = parameters->MultiSampleQuality;
    if (!emit_command(D9WG_OP_CREATE_DEVICE, &command, sizeof(command))) {
        IDirect3D9_Release(iface);
        HeapFree(GetProcessHeap(), 0, device);
        TRACE("FAIL CreateDevice transport -> %08lX",
            (DWORD)D3DERR_DRIVERINTERNALERROR);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    if (!emit_render_default_patches(device->handle, 9, device->present.EnableAutoDepthStencil, device->render_states)
        || !emit_object_default_patches(device->handle, &device->material)) {
        IDirect3DDevice9_Release(&device->iface);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }

    if (!reset_window_mode(device, window)) {
        IDirect3DDevice9_Release(&device->iface);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    emit_window_state(device, window);
    watch_window_state(device);
    d9wg_present_parameters_return(9, parameters, &device->present);
    *device_out = &device->iface;
#ifdef D9WG_DIAGNOSTIC_TRACE
    g_trace_device_window = window;
#endif
    TRACE("OK CreateDevice handle=%08lX surface=%lux%lu hwnd=%08lX",
        device->handle, command.width, command.height,
        (DWORD)(uintptr_t)window);
    return D3D_OK;
}
