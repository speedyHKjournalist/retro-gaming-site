/* Actual legacy8 additional-chain constructor,site70b8598. */
static HRESULT WINAPI device_create_swapchain(IDirect3DDevice8 *iface,
        D3DPRESENT_PARAMETERS *parameters, IDirect3DSwapChain8 **chain_out)
{
    D8Device *device = device_from_iface(iface);
    D8SwapChain *chain;
    HWND window;
    RECT client;
    UINT width, height;
    D3DFORMAT format;
    D9WGCreateSwapChain command;
    POINT origin = { 0, 0 };
    HRESULT hr;
    if (!chain_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *chain_out = NULL;
    if (!parameters || !parameters->Windowed
            || !supported_multisample_type(parameters->MultiSampleType)
            || parameters->BackBufferCount > 3
            || parameters->BackBufferWidth > 8192
            || parameters->BackBufferHeight > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!d9wg_present_interval_valid(8, parameters->Windowed,
            parameters->FullScreen_PresentationInterval, D9WG_PRESENT_INTERVAL_MASK))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!d9wg_swap_effect_valid(8, parameters->SwapEffect, parameters->BackBufferCount))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!d9wg_present_multisample_valid(8, parameters->SwapEffect,
            parameters->MultiSampleType, 0, parameters->Flags))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
            && !supported_backbuffer_format(parameters->BackBufferFormat))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    window = parameters->hDeviceWindow ? parameters->hDeviceWindow
                                       : device->creation.hFocusWindow;
    SetRect(&client, 0, 0, (int)device->display_mode.Width,
        (int)device->display_mode.Height);
    if (window)
        GetClientRect(window, &client);
    width = d9wg_present_extent(parameters->BackBufferWidth,
        (uint32_t)(client.right - client.left), device->display_mode.Width);
    height = d9wg_present_extent(parameters->BackBufferHeight,
        (uint32_t)(client.bottom - client.top), device->display_mode.Height);
    if (width > 8192 || height > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    format = parameters->BackBufferFormat == D3DFMT_UNKNOWN
        ? device->display_mode.Format : parameters->BackBufferFormat;
    chain = (D8SwapChain *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*chain));
    if (!chain) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    chain->iface.lpVtbl = &g_swapchain_vtbl;
    chain->refcount = 1;
    chain->device = device;
    d9wg_d3d9_core_parameters_effective8(&chain->present, parameters,
        width, height, format, window);
    /* D3D8 permits the presentation flag with MSAA, but its buffers remain
     * non-lockable. The standalone render-target constructor rejects that
     * flag combination, so only pass it through for single-sample storage. */
    hr = device_create_render_target(iface, width, height, format,
        parameters->MultiSampleType,
        (parameters->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) != 0
            && parameters->MultiSampleType == D3DMULTISAMPLE_NONE,
        &chain->back_buffer);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, chain);
        return hr;
    }
    D8Surface *surface = surface_from_iface(chain->back_buffer);
    chain->handle = allocate_handle();
    if (window)
        ClientToScreen(window, &origin);
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;
    command.swap_chain_handle = chain->handle;
    command.back_buffer_handle = surface->texture ? surface->texture->handle : 0;
    command.hwnd = (uint32_t)(uintptr_t)window;
    command.x = origin.x;
    command.y = origin.y;
    command.width = width;
    command.height = height;
    if (!command.back_buffer_handle
        || !emit_command(D9WG_OP_CREATE_SWAP_CHAIN, &command, sizeof(command))) {
        IDirect3DSurface8_Release(chain->back_buffer);
        HeapFree(GetProcessHeap(), 0, chain);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    /* Transfer the standalone surface's public pin to the chain, keeping its
     * storage reference as the chain's base reference. Public views acquire
     * their own pin only while their independent count is nonzero. */
    surface->texture->private_back_buffer = TRUE;
    surface->texture->swap_chain = chain;
    surface->additional_back_buffer = TRUE;
    surface->refcount = 0;
    ++device->additional_swap_chain_count;
    d9wg_present_parameters_return(8, parameters, &chain->present);
    *chain_out = &chain->iface;
    return D3D_OK;
}
