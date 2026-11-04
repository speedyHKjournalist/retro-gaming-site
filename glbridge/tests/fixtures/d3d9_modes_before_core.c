/* Actual previous implementation from site 10ce33d, SDK9. Positive oracle only. */
static UINT WINAPI d3d_get_adapter_mode_count_before9(IDirect3D9 *iface, UINT adapter,
        D3DFORMAT format)
{
    UINT count;
    (void)iface;

    if (adapter || (format != D3DFMT_X8R8G8B8 && format != D3DFMT_R5G6B5)) {
        /* A zero here empties the app's video-mode list for that format, which
         * is a common reason to abort startup before drawing anything. */
        TRACE("OK GetAdapterModeCount adapter=%lu format=%08lX -> 0", adapter,
                (DWORD)format);
        return 0;
    }
    count = enumerate_adapter_modes_before9(format, 0, NULL);
    TRACE("OK GetAdapterModeCount adapter=%lu format=%08lX -> %lu", adapter,
            (DWORD)format, count);
    return count;
}

static HRESULT WINAPI d3d_enum_adapter_modes_before9(IDirect3D9 *iface, UINT adapter,
        D3DFORMAT format, UINT index, D3DDISPLAYMODE *mode)
{
    (void)iface;

    if (adapter || !mode || (format != D3DFMT_X8R8G8B8
            && format != D3DFMT_R5G6B5)
            || !enumerate_adapter_modes_before9(format, index, mode)) {
        TRACE("FAIL EnumAdapterModes adapter=%lu format=%08lX index=%lu -> %08lX",
                adapter, (DWORD)format, index, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    TRACE("OK EnumAdapterModes format=%08lX index=%lu -> %lux%lu refresh=%lu",
            (DWORD)format, index, mode->Width, mode->Height,
            mode->RefreshRate);
    return D3D_OK;
}

static UINT enumerate_adapter_modes_before9(D3DFORMAT format, UINT wanted,
        D3DDISPLAYMODE *out)
{
    DEVMODEA current;
    DWORD bits_per_pixel = (format == D3DFMT_R5G6B5) ? 16u : 32u;
    DWORD index = 0;
    UINT matched = 0;

    for (;;) {
        ZeroMemory(&current, sizeof(current));
        current.dmSize = sizeof(current);
        if (!EnumDisplaySettingsA(NULL, index++, &current))
            break;
        if (current.dmBitsPerPel != bits_per_pixel)
            continue;
        if (!current.dmPelsWidth || !current.dmPelsHeight)
            continue;
        if (out && matched == wanted) {
            fill_display_mode(out, current.dmPelsWidth, current.dmPelsHeight,
                    format);
            /* 0 and 1 are the driver's "default/unspecified" encodings. */
            if (current.dmDisplayFrequency > 1)
                out->RefreshRate = current.dmDisplayFrequency;
            return matched + 1;
        }
        ++matched;
    }
    /* Counting returns the complete list length. Fetching must instead report
     * whether the requested index was found. Returning `matched` here made
     * every index past the end look successful whenever the adapter had at
     * least one mode; Warcraft III consequently enumerated forever and never
     * reached CreateDevice. */
    return out ? 0 : matched;
}
