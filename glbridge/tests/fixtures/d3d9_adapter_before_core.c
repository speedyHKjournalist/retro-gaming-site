/* Actual previous implementation from site a91fa5f, SDK9. Positive oracle only. */
static HRESULT WINAPI d3d_get_adapter_identifier_before9(IDirect3D9 *iface,
        UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 *identifier)
{
    (void)iface; (void)flags;

    TRACE("CALL GetAdapterIdentifier adapter=%lu flags=%08lX", adapter, flags);
    if (adapter || !identifier) {
        TRACE("FAIL GetAdapterIdentifier -> %08lX", (DWORD)D3DERR_INVALIDCALL);
        return D3DERR_INVALIDCALL;
    }
    ZeroMemory(identifier, sizeof(*identifier));
    /*
     * Fields other than the identity triple are the same in every variant:
     * no real driver reports an all-zero DriverVersion, an all-zero
     * DeviceIdentifier GUID (apps cache it to notice a driver change) or an
     * empty DeviceName, and a caller that sanity-checks the struct would
     * reject those regardless of which card we claim to be.
     */
    lstrcpynA(identifier->DeviceName, "\\\\.\\DISPLAY1",
            sizeof(identifier->DeviceName));
#if D9_ADAPTER_IDENTITY == D9_ADAPTER_GEFORCEFX_5200
    /* NV34. The entry-level card of the first NVIDIA generation with
     * vs_2_0/ps_2_0, matching the shader model fill_caps() reports from M2
     * on. */
    lstrcpynA(identifier->Driver, "nv4_disp.dll", sizeof(identifier->Driver));
    lstrcpynA(identifier->Description, "NVIDIA GeForce FX 5200",
            sizeof(identifier->Description));
    identifier->VendorId = 0x10DE;
    identifier->DeviceId = 0x0322;
    identifier->SubSysId = 0x032210DE;
    identifier->Revision = 0xA1;
    /* ForceWare 66.93, a real XP driver revision for this card. */
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (10 << 16) | 6693;
#elif D9_ADAPTER_IDENTITY == D9_ADAPTER_GEFORCE4_MX
    /* NV17. Hardware T&L, no programmable shaders -- the one widely-known
     * card whose real capabilities match what fill_caps() reports. */
    lstrcpynA(identifier->Driver, "nv4_disp.dll", sizeof(identifier->Driver));
    lstrcpynA(identifier->Description, "NVIDIA GeForce4 MX 440",
            sizeof(identifier->Description));
    identifier->VendorId = 0x10DE;
    identifier->DeviceId = 0x0171;
    identifier->SubSysId = 0x017110DE;
    identifier->Revision = 0xA3;
    /* ForceWare 45.23, a real driver revision for this card on XP. */
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (10 << 16) | 4523;
#elif D9_ADAPTER_IDENTITY == D9_ADAPTER_VMWARE_SVGA
    lstrcpynA(identifier->Driver, "vm3dum.dll", sizeof(identifier->Driver));
    lstrcpynA(identifier->Description, "VMware SVGA 3D",
            sizeof(identifier->Description));
    identifier->VendorId = 0x15AD;
    identifier->DeviceId = 0x0405;
    identifier->SubSysId = 0x040515AD;
    identifier->Revision = 0;
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (1 << 16) | 1264;
#else
    lstrcpynA(identifier->Driver, "d3d9-webgpu.dll",
            sizeof(identifier->Driver));
    lstrcpynA(identifier->Description, "v86 Direct3D 9 WebGPU Adapter",
            sizeof(identifier->Description));
    identifier->VendorId = 0x1234;
    identifier->DeviceId = 0x5687;
    identifier->SubSysId = 0x56871234;
    identifier->Revision = 1;
    /* product.version.subversion.build packed as two DWORDs, the way real
     * drivers report it (here 6.14.10.6764, a plausible XP-era driver). */
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (10 << 16) | 6764;
#endif
    /* Stable and non-zero; derived from the identity so switching variants
     * also looks like a different driver, which is what an app caching this
     * field would expect. */
    identifier->DeviceIdentifier.Data1 = 0xD9E60000u | identifier->DeviceId;
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
    /* 1 = WHQL-signed but no date info, rather than 0 = "not certified",
     * which some titles treat as an unusable/blacklisted driver. */
    identifier->WHQLLevel = 1;

    TRACE("OK GetAdapterIdentifier vendor=%04lX device=%04lX desc=%s",
            identifier->VendorId, identifier->DeviceId,
            identifier->Description);
    return D3D_OK;
}

static HRESULT WINAPI d3d_get_adapter_display_mode_before9(IDirect3D9* iface,
    UINT adapter, D3DDISPLAYMODE* mode)
{
    (void)iface;

    if (adapter || !mode)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    D9WGDisplayMode current;
    d9wg_display_mode_current(&current);
    fill_display_mode(mode, current.width, current.height, (D3DFORMAT)current.format);
    mode->RefreshRate = current.refresh_rate;
    TRACE("OK GetAdapterDisplayMode -> %lux%lu format=%08lX refresh=%lu",
        mode->Width, mode->Height, (DWORD)mode->Format, mode->RefreshRate);
    return D3D_OK;
}

static HRESULT WINAPI d3d_register_software_device_before9(IDirect3D9 *iface,
        void *initialize)
{
    (void)iface; (void)initialize;

    TRACE("FAIL RegisterSoftwareDevice -> %08lX", (DWORD)D3DERR_INVALIDCALL);
    return D3DERR_INVALIDCALL;
}
