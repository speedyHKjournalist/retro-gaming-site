/* Actual ordinary9 additional-chain constructor,site70b8598. */
static HRESULT WINAPI device_create_additional_swap_chain(
        IDirect3DDevice9 *iface, D3DPRESENT_PARAMETERS *params,
        IDirect3DSwapChain9 **out)
{
    D9Device *device = device_from_iface(iface);
    D9SwapChain *chain;
    D9WGCreateSwapChain command;
    IDirect3DSurface9 *back_buffer = NULL;
    D9Surface *surface;
    HWND window;
    UINT width;
    UINT height;
    D3DFORMAT format;
    RECT client;
    POINT origin;
    HRESULT hr;

    TRACE_MARK_ENTER("Device.CreateAdditionalSwapChain");
    if (out) *out = NULL;
    if (!params || !out) {
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (!d9wg_present_interval_valid(9, params->Windowed,
            params->PresentationInterval, D9WG_PRESENT_INTERVAL_MASK))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_swap_effect_valid(9, params->SwapEffect, params->BackBufferCount))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_present_multisample_valid(9, params->SwapEffect,
            params->MultiSampleType, params->MultiSampleQuality, params->Flags))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (params->BackBufferCount > 3)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!params->Windowed || !device->present.Windowed) {
        HOSTLOG_REFUSED("CreateAdditionalSwapChain refused: D3D9 allows "
                "additional chains only on a windowed device, and only for a "
                "windowed chain");
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }

    window = params->hDeviceWindow ? params->hDeviceWindow
            : device->creation.hFocusWindow;
    if (!window) {
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    /* D3D9 fills a zero width/height from the window's client area. */
    probe_window_rect(window, (int)device->display_mode.Width,
            (int)device->display_mode.Height, &client, &origin);
    width = params->BackBufferWidth ? params->BackBufferWidth
            : (UINT)(client.right - client.left);
    height = params->BackBufferHeight ? params->BackBufferHeight
            : (UINT)(client.bottom - client.top);
    if (!width || !height) {
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    format = (D3DFORMAT)d9wg_display_mode_backbuffer_format(
        params->BackBufferFormat);

    if (!supported_multisample(format, params->MultiSampleType))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    hr = create_target_texture(device, width, height, format,
            D3DUSAGE_RENDERTARGET, params->MultiSampleType,
            params->MultiSampleQuality, &back_buffer);
    if (FAILED(hr)) {
        HOSTLOG_FAILED("CreateAdditionalSwapChain refused: its back buffer "
                "(%lux%lu format=%08lX) could not be created", width, height,
                (DWORD)format);
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain", hr, NULL);
        return hr;
    }
    surface = surface_from_iface(back_buffer);

    chain = (D9SwapChain *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*chain));
    if (!chain) {
        IDirect3DSurface9_Release(back_buffer);
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain", E_OUTOFMEMORY,
                NULL);
        return E_OUTOFMEMORY;
    }
    chain->iface.lpVtbl = &g_swap_chain_vtbl;
    chain->refcount = 1;
    chain->device = device;
    chain->handle = allocate_handle();
    chain->window = window;
    chain->back_buffer = back_buffer;
    d9wg_present_parameters_effective(&chain->present, params,
        width, height, format, window);

    command.device_handle = device->handle;
    command.swap_chain_handle = chain->handle;
    command.back_buffer_handle = surface->texture ? surface->texture->handle : 0;
    command.hwnd = (uint32_t)(uintptr_t)window;
    command.x = origin.x;
    command.y = origin.y;
    command.width = width;
    command.height = height;
    if (!command.back_buffer_handle
            || !emit_command(D9WG_OP_CREATE_SWAP_CHAIN, &command,
                    sizeof(command))) {
        IDirect3DSurface9_Release(back_buffer);
        HeapFree(GetProcessHeap(), 0, chain);
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_DRIVERINTERNALERROR, NULL);
        return D3DERR_DRIVERINTERNALERROR;
    }

    chain->next_device_chain = device->additional_swap_chains;
    device->additional_swap_chains = chain;
    /* Transfer the standalone surface's one device pin to the public chain.
     * Its private texture storage has no separate device pin. */
    surface->swap_chain = chain;
    surface->refcount = 0;
    d9wg_present_parameters_return(9, params, &chain->present);
    *out = &chain->iface;
    TRACE("OK CreateAdditionalSwapChain device=%08lX chain=%08lX back=%08lX "
            "%lux%lu window=%08lX", device->handle, chain->handle,
            command.back_buffer_handle, width, height,
            (DWORD)(uintptr_t)window);
    TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain", D3D_OK, *out);
    return D3D_OK;
}
