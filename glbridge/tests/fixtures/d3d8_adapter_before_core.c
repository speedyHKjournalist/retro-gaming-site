/* Actual previous implementation from site a91fa5f, SDK8. Positive oracle only. */
static HRESULT WINAPI d3d_get_adapter_identifier_before8(IDirect3D8 *iface,
        UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER8 *identifier)
{
    (void)iface;
    if (adapter || !identifier)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    ZeroMemory(identifier, sizeof(*identifier));
    lstrcpynA(identifier->Driver, "d3d8-webgpu", sizeof(identifier->Driver));
    lstrcpynA(identifier->Description,
            "v86 Direct3D 8 WebGPU Adapter", sizeof(identifier->Description));
    identifier->VendorId = 0x1234;
    identifier->DeviceId = 0x5686;
    identifier->SubSysId = 0x56861234;
    identifier->Revision = 1;
    /* No real driver reports an all-zero DriverVersion, and a caller that
     * sanity-checks the struct -- or prints it, as 3DMark 2001's system-info
     * report does -- has nothing to show for it. product.version.subversion.
     * build packed as two DWORDs, the way a driver reports it; 6.14.10.6764
     * is a plausible XP-era revision and is what ../d3d9proxy reports for the
     * same backend. */
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (10 << 16) | 6764;
    /* Stable and non-zero, derived from the identity: apps cache this GUID to
     * notice that the driver changed under them, which an all-zero value
     * defeats. The D9WG frontend derives its own the same way, from a
     * different prefix, so the two never collide. */
    identifier->DeviceIdentifier.Data1 = 0xD8E60000u | identifier->DeviceId;
    identifier->DeviceIdentifier.Data2 = (WORD)identifier->VendorId;
    identifier->DeviceIdentifier.Data3 = (WORD)identifier->DeviceId;
    identifier->DeviceIdentifier.Data4[0] = 0x9A;
    identifier->DeviceIdentifier.Data4[1] = 0xB1;
    identifier->DeviceIdentifier.Data4[2] = 0xC2;
    identifier->DeviceIdentifier.Data4[3] = 0xD3;
    identifier->DeviceIdentifier.Data4[4] = 0xE4;
    identifier->DeviceIdentifier.Data4[5] = 0xF5;
    identifier->DeviceIdentifier.Data4[6] = 0x06;
    identifier->DeviceIdentifier.Data4[7] = 0x17;
    /* D3D8 computes the WHQL level only when the caller leaves
     * D3DENUM_NO_WHQL_LEVEL clear. 1 = signed but no date information, rather
     * than 0 = "not certified", which some titles read as a blacklisted
     * driver. */
    identifier->WHQLLevel = (flags & D3DENUM_NO_WHQL_LEVEL) ? 0 : 1;
    return D3D_OK;
}

static HRESULT WINAPI d3d_get_adapter_display_mode_before8(IDirect3D8* iface,
    UINT adapter, D3DDISPLAYMODE* mode)
{
    (void)iface;
    if (adapter || !mode)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    D9WGDisplayMode current;
    d9wg_display_mode_current(&current);
    fill_display_mode(mode, current.width, current.height, (D3DFORMAT)current.format);
    mode->RefreshRate = current.refresh_rate;
    return D3D_OK;
}

static HRESULT WINAPI d3d_register_software_device_before8(IDirect3D8 *iface,
        void *initialize)
{
    (void)iface;
    (void)initialize;
    return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
}
