/*
 * Direct3D 1-6 compatibility object model.
 *
 * DirectX 5 and 6 both expose IDirect3D3.  All legacy interface views below
 * share the Direct3D 7 device, surface and vertex-buffer allocations, so state
 * changes and draws take exactly the same WebGPU command path as Direct3D 7.
 */

typedef struct D3DLegacyExecuteBuffer D3DLegacyExecuteBuffer;

struct D3DLegacyMaterial {
    const IDirect3DMaterial3Vtbl *vtbl3;
    const IDirect3DMaterial2Vtbl *vtbl2;
    const IDirect3DMaterialVtbl *vtbl1;
    LONG ref;
    DDrawObject *owner;
    D3DMATERIAL material;
    D3DMATERIALHANDLE handle;
    D3DLegacyMaterial *next;
};

struct D3DLegacyLight {
    const IDirect3DLightVtbl *vtbl;
    LONG ref;
    DDrawObject *owner;
    D3DLIGHT2 light;
    D3DLegacyViewport *viewport;
    D3DLegacyLight *next_in_viewport;
};

struct D3DLegacyViewport {
    const IDirect3DViewport3Vtbl *vtbl3;
    const IDirect3DViewport2Vtbl *vtbl2;
    const IDirect3DViewportVtbl *vtbl1;
    LONG ref;
    DDrawObject *owner;
    D3D7Device *device;
    D3DVIEWPORT viewport1;
    D3DVIEWPORT2 viewport2;
    BOOL viewport1_set;
    BOOL viewport2_set;
    D3DMATERIALHANDLE background;
    BOOL background_valid;
    DDSurface *background_depth;
    D3DLegacyLight *lights;
    D3DLegacyViewport *next_in_device;
};

struct D3DLegacyMatrix {
    D3DMATRIXHANDLE handle;
    D3DMATRIX matrix;
    D3DLegacyMatrix *next;
};

struct D3DLegacyExecuteBuffer {
    const IDirect3DExecuteBufferVtbl *vtbl;
    LONG ref;
    D3D7Device *device;
    BYTE *memory;
    DWORD byte_count;
    DWORD caps;
    BOOL locked;
    D3DEXECUTEDATA data;
    D3DTLVERTEX *processed;
    DWORD processed_count;
};

static IDirect3DMaterial3Vtbl g_d3d_material3_vtbl;
static IDirect3DMaterial2Vtbl g_d3d_material2_vtbl;
static IDirect3DMaterialVtbl g_d3d_material1_vtbl;
static IDirect3DLightVtbl g_d3d_light_vtbl;
static IDirect3DViewport3Vtbl g_d3d_viewport3_vtbl;
static IDirect3DViewport2Vtbl g_d3d_viewport2_vtbl;
static IDirect3DViewportVtbl g_d3d_viewport1_vtbl;
static IDirect3DExecuteBufferVtbl g_d3d_execute_buffer_vtbl;
static HRESULT d3d_legacy_apply_viewport(D3DLegacyViewport *viewport);

static DDrawObject *d3d3_object_from_iface(IDirect3D3 *iface)
{
    return (DDrawObject *)((BYTE *)iface - offsetof(DDrawObject, d3d3_vtbl));
}

static DDrawObject *d3d2_object_from_iface(IDirect3D2 *iface)
{
    return (DDrawObject *)((BYTE *)iface - offsetof(DDrawObject, d3d2_vtbl));
}

static DDrawObject *d3d1_object_from_iface(IDirect3D *iface)
{
    return (DDrawObject *)((BYTE *)iface - offsetof(DDrawObject, d3d1_vtbl));
}

static D3D7Device *d3d3_device_from_iface(IDirect3DDevice3 *iface)
{
    return (D3D7Device *)((BYTE *)iface -
            offsetof(D3D7Device, device3_vtbl));
}

static D3D7Device *d3d2_device_from_iface(IDirect3DDevice2 *iface)
{
    return (D3D7Device *)((BYTE *)iface -
            offsetof(D3D7Device, device2_vtbl));
}

static D3D7Device *d3d1_device_from_iface(IDirect3DDevice *iface)
{
    return (D3D7Device *)((BYTE *)iface -
            offsetof(D3D7Device, device1_vtbl));
}

static D3D7VertexBuffer *d3d_vb_from_iface(IDirect3DVertexBuffer *iface)
{
    return (D3D7VertexBuffer *)((BYTE *)iface -
            offsetof(D3D7VertexBuffer, legacy_vtbl));
}

static DDSurface *d3d_texture2_from_iface(IDirect3DTexture2 *iface)
{
    return (DDSurface *)((BYTE *)iface - offsetof(DDSurface, texture2_vtbl));
}

static DDSurface *d3d_texture1_from_iface(IDirect3DTexture *iface)
{
    return (DDSurface *)((BYTE *)iface - offsetof(DDSurface, texture1_vtbl));
}

static D3DLegacyMaterial *d3d_material_from_any(void *iface)
{
    const void *vtbl = *(const void **)iface;
    SIZE_T offset = offsetof(D3DLegacyMaterial, vtbl3);
    if (vtbl == (const void *)&g_d3d_material2_vtbl)
        offset = offsetof(D3DLegacyMaterial, vtbl2);
    else if (vtbl == (const void *)&g_d3d_material1_vtbl)
        offset = offsetof(D3DLegacyMaterial, vtbl1);
    return (D3DLegacyMaterial *)((BYTE *)iface - offset);
}

static D3DLegacyViewport *d3d_viewport_from_any(void *iface)
{
    const void *vtbl = *(const void **)iface;
    SIZE_T offset = offsetof(D3DLegacyViewport, vtbl3);
    if (vtbl == (const void *)&g_d3d_viewport2_vtbl)
        offset = offsetof(D3DLegacyViewport, vtbl2);
    else if (vtbl == (const void *)&g_d3d_viewport1_vtbl)
        offset = offsetof(D3DLegacyViewport, vtbl1);
    return (D3DLegacyViewport *)((BYTE *)iface - offset);
}

static DWORD d3d_legacy_vertex_type_fvf(D3DVERTEXTYPE type)
{
    switch (type) {
    case D3DVT_VERTEX: return D3DFVF_VERTEX;
    case D3DVT_LVERTEX: return D3DFVF_LVERTEX;
    case D3DVT_TLVERTEX: return D3DFVF_TLVERTEX;
    default: return 0;
    }
}

static void d3d_legacy_fill_caps(D3DDEVICEDESC *caps)
{
    D3DDEVICEDESC7 caps7;
    ZeroMemory(caps, sizeof(*caps));
    d3d7_fill_caps(&caps7);
    caps->dwSize = sizeof(*caps);
    caps->dwFlags = D3DDD_COLORMODEL | D3DDD_DEVCAPS |
            D3DDD_TRANSFORMCAPS | D3DDD_LIGHTINGCAPS | D3DDD_BCLIPPING |
            D3DDD_LINECAPS | D3DDD_TRICAPS | D3DDD_DEVICERENDERBITDEPTH |
            D3DDD_DEVICEZBUFFERBITDEPTH | D3DDD_MAXBUFFERSIZE |
            D3DDD_MAXVERTEXCOUNT;
    caps->dcmColorModel = D3DCOLOR_RGB;
    caps->dwDevCaps = caps7.dwDevCaps | D3DDEVCAPS_EXECUTESYSTEMMEMORY |
            D3DDEVCAPS_EXECUTEVIDEOMEMORY;
    caps->dtcTransformCaps.dwSize = sizeof(caps->dtcTransformCaps);
    caps->dtcTransformCaps.dwCaps = D3DTRANSFORMCAPS_CLIP;
    caps->bClipping = TRUE;
    caps->dlcLightingCaps.dwSize = sizeof(caps->dlcLightingCaps);
    caps->dlcLightingCaps.dwCaps = D3DLIGHTCAPS_POINT |
            D3DLIGHTCAPS_SPOT | D3DLIGHTCAPS_DIRECTIONAL |
            D3DLIGHTCAPS_PARALLELPOINT | D3DLIGHTCAPS_GLSPOT;
    caps->dlcLightingCaps.dwLightingModel = D3DLIGHTINGMODEL_RGB;
    caps->dlcLightingCaps.dwNumLights = D3D7_MAX_LIGHTS;
    caps->dpcLineCaps = caps7.dpcLineCaps;
    caps->dpcTriCaps = caps7.dpcTriCaps;
    caps->dwDeviceRenderBitDepth = caps7.dwDeviceRenderBitDepth;
    caps->dwDeviceZBufferBitDepth = caps7.dwDeviceZBufferBitDepth;
    caps->dwMaxBufferSize = 16u * 1024u * 1024u;
    caps->dwMaxVertexCount = D3DMAXNUMVERTICES;
    caps->dwMinTextureWidth = caps7.dwMinTextureWidth;
    caps->dwMinTextureHeight = caps7.dwMinTextureHeight;
    caps->dwMaxTextureWidth = caps7.dwMaxTextureWidth;
    caps->dwMaxTextureHeight = caps7.dwMaxTextureHeight;
    caps->dwMaxTextureRepeat = caps7.dwMaxTextureRepeat;
    caps->dwMaxTextureAspectRatio = caps7.dwMaxTextureAspectRatio;
    caps->dwMaxAnisotropy = caps7.dwMaxAnisotropy;
    caps->dvGuardBandLeft = caps7.dvGuardBandLeft;
    caps->dvGuardBandTop = caps7.dvGuardBandTop;
    caps->dvGuardBandRight = caps7.dvGuardBandRight;
    caps->dvGuardBandBottom = caps7.dvGuardBandBottom;
    caps->dvExtentsAdjust = caps7.dvExtentsAdjust;
    caps->dwStencilCaps = caps7.dwStencilCaps;
    caps->dwFVFCaps = caps7.dwFVFCaps;
    caps->dwTextureOpCaps = caps7.dwTextureOpCaps;
    caps->wMaxTextureBlendStages = caps7.wMaxTextureBlendStages;
    caps->wMaxSimultaneousTextures = caps7.wMaxSimultaneousTextures;
}

static HRESULT d3d_legacy_enum_devices(LPD3DENUMDEVICESCALLBACK callback,
        void *context)
{
    D3DDEVICEDESC hal, hel;
    GUID guid;
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    d3d_legacy_fill_caps(&hal);
    hel = hal;
    guid = IID_IDirect3DHALDevice;
    if (callback(&guid, (char *)"v86 WebGPU HAL",
            (char *)"v86 WebGPU Direct3D 1-6 HAL", &hal, &hel, context)
            == D3DENUMRET_CANCEL) return D3D_OK;
    guid = IID_IDirect3DRGBDevice;
    if (callback(&guid, (char *)"v86 WebGPU RGB",
            (char *)"v86 WebGPU Direct3D RGB", &hel, &hel, context)
            == D3DENUMRET_CANCEL) return D3D_OK;
    guid = IID_IDirect3DRampDevice;
    if (callback(&guid, (char *)"v86 WebGPU ramp compatibility",
            (char *)"v86 WebGPU Direct3D ramp", &hel, &hel, context)
            == D3DENUMRET_CANCEL) return D3D_OK;
    guid = IID_IDirect3DMMXDevice;
    callback(&guid, (char *)"v86 WebGPU MMX compatibility",
            (char *)"v86 WebGPU Direct3D MMX", &hel, &hel, context);
    return D3D_OK;
}

static HRESULT d3d_legacy_find_device(D3DFINDDEVICESEARCH *search,
        D3DFINDDEVICERESULT *result)
{
    if (!search || !result || search->dwSize != sizeof(*search) ||
            result->dwSize != sizeof(*result))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    ZeroMemory(result, sizeof(*result));
    result->dwSize = sizeof(*result);
    result->guid = IID_IDirect3DHALDevice;
    d3d_legacy_fill_caps(&result->ddHwDesc);
    result->ddSwDesc = result->ddHwDesc;
    return D3D_OK;
}

static HRESULT WINAPI d3d3_QueryInterface(IDirect3D3 *iface, REFIID iid,
        void **out)
{
    return ddraw_QueryInterface((IDirectDraw7 *)&
            d3d3_object_from_iface(iface)->vtbl7, iid, out);
}

static ULONG WINAPI d3d3_AddRef(IDirect3D3 *iface)
{
    return ddraw_AddRef((IDirectDraw7 *)&
            d3d3_object_from_iface(iface)->vtbl7);
}

static ULONG WINAPI d3d3_Release(IDirect3D3 *iface)
{
    return ddraw_Release((IDirectDraw7 *)&
            d3d3_object_from_iface(iface)->vtbl7);
}

static HRESULT WINAPI d3d3_EnumDevices(IDirect3D3 *iface,
        LPD3DENUMDEVICESCALLBACK callback, void *context)
{
    (void)iface;
    return d3d_legacy_enum_devices(callback, context);
}

static HRESULT WINAPI d3d3_FindDevice(IDirect3D3 *iface,
        D3DFINDDEVICESEARCH *search, D3DFINDDEVICERESULT *result)
{
    (void)iface;
    return d3d_legacy_find_device(search, result);
}

static HRESULT WINAPI d3d2_QueryInterface(IDirect3D2 *iface, REFIID iid,
        void **out)
{
    return ddraw_QueryInterface((IDirectDraw7 *)&
            d3d2_object_from_iface(iface)->vtbl7, iid, out);
}

static ULONG WINAPI d3d2_AddRef(IDirect3D2 *iface)
{
    return ddraw_AddRef((IDirectDraw7 *)&
            d3d2_object_from_iface(iface)->vtbl7);
}

static ULONG WINAPI d3d2_Release(IDirect3D2 *iface)
{
    return ddraw_Release((IDirectDraw7 *)&
            d3d2_object_from_iface(iface)->vtbl7);
}

static HRESULT WINAPI d3d2_EnumDevices(IDirect3D2 *iface,
        LPD3DENUMDEVICESCALLBACK callback, void *context)
{
    (void)iface;
    return d3d_legacy_enum_devices(callback, context);
}

static HRESULT WINAPI d3d2_FindDevice(IDirect3D2 *iface,
        D3DFINDDEVICESEARCH *search, D3DFINDDEVICERESULT *result)
{
    (void)iface;
    return d3d_legacy_find_device(search, result);
}

static HRESULT WINAPI d3d1_QueryInterface(IDirect3D *iface, REFIID iid,
        void **out)
{
    return ddraw_QueryInterface((IDirectDraw7 *)&
            d3d1_object_from_iface(iface)->vtbl7, iid, out);
}

static ULONG WINAPI d3d1_AddRef(IDirect3D *iface)
{
    return ddraw_AddRef((IDirectDraw7 *)&
            d3d1_object_from_iface(iface)->vtbl7);
}

static ULONG WINAPI d3d1_Release(IDirect3D *iface)
{
    return ddraw_Release((IDirectDraw7 *)&
            d3d1_object_from_iface(iface)->vtbl7);
}

static HRESULT WINAPI d3d1_Initialize(IDirect3D *iface, REFIID iid)
{
    (void)iface;
    return DDWG_TRACE_ERROR(iid ? D3D_OK : DDERR_INVALIDPARAMS);
}

static HRESULT WINAPI d3d1_EnumDevices(IDirect3D *iface,
        LPD3DENUMDEVICESCALLBACK callback, void *context)
{
    (void)iface;
    return d3d_legacy_enum_devices(callback, context);
}

static HRESULT WINAPI d3d1_FindDevice(IDirect3D *iface,
        D3DFINDDEVICESEARCH *search, D3DFINDDEVICERESULT *result)
{
    (void)iface;
    return d3d_legacy_find_device(search, result);
}

/* ------------------------------------------------------------------ *
 * Material and light objects
 * ------------------------------------------------------------------ */

static void d3d_legacy_material7(const D3DMATERIAL *source,
        D3DMATERIAL7 *destination)
{
    destination->diffuse = source->diffuse;
    destination->ambient = source->ambient;
    destination->specular = source->specular;
    destination->emissive = source->emissive;
    destination->power = source->power;
}

static D3DLegacyMaterial *d3d_legacy_find_material(DDrawObject *owner,
        D3DMATERIALHANDLE handle)
{
    D3DLegacyMaterial *material;
    for (material = owner->legacy_materials; material;
            material = material->next) {
        if (material->handle == handle) return material;
    }
    return NULL;
}

static HRESULT WINAPI d3d_material_QueryInterface(IDirect3DMaterial3 *iface,
        REFIID iid, void **out)
{
    D3DLegacyMaterial *material = d3d_material_from_any(iface);
    if (!out) return DDWG_TRACE_ERROR(E_POINTER);
    *out = NULL;
    if (iid_is_unknown(iid) || guid_equal(iid, &IID_IDirect3DMaterial3))
        *out = &material->vtbl3;
    else if (guid_equal(iid, &IID_IDirect3DMaterial2))
        *out = &material->vtbl2;
    else if (guid_equal(iid, &IID_IDirect3DMaterial))
        *out = &material->vtbl1;
    else
        return DDWG_TRACE_ERROR(E_NOINTERFACE);
    InterlockedIncrement(&material->ref);
    return S_OK;
}

static ULONG WINAPI d3d_material_AddRef(IDirect3DMaterial3 *iface)
{
    return (ULONG)InterlockedIncrement(&d3d_material_from_any(iface)->ref);
}

static ULONG WINAPI d3d_material_Release(IDirect3DMaterial3 *iface)
{
    D3DLegacyMaterial *material = d3d_material_from_any(iface);
    LONG remaining = InterlockedDecrement(&material->ref);
    if (!remaining) {
        DDrawObject *owner = material->owner;
        D3DLegacyMaterial **link = &owner->legacy_materials;
        while (*link && *link != material) link = &(*link)->next;
        if (*link) *link = material->next;
        heap_free(material);
        ddraw_Release((IDirectDraw7 *)&owner->vtbl7);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

static HRESULT WINAPI d3d_material_SetMaterial(IDirect3DMaterial3 *iface,
        D3DMATERIAL *data)
{
    D3DLegacyMaterial *material = d3d_material_from_any(iface);
    D3DMATERIAL7 material7;
    if (!data || data->dwSize < sizeof(D3DMATERIAL))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    material->material = *data;
    material->material.dwSize = sizeof(material->material);
    if (material->owner->d3d7_device) {
        d3d_legacy_material7(&material->material, &material7);
        if (material->owner->d3d7_device->legacy_material == material)
            return d3d7_device_SetMaterial((IDirect3DDevice7 *)
                    material->owner->d3d7_device, &material7);
    }
    return D3D_OK;
}

static HRESULT WINAPI d3d_material_GetMaterial(IDirect3DMaterial3 *iface,
        D3DMATERIAL *data)
{
    D3DLegacyMaterial *material = d3d_material_from_any(iface);
    DWORD size;
    if (!data || data->dwSize < sizeof(DWORD)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    size = data->dwSize < sizeof(material->material) ? data->dwSize :
            sizeof(material->material);
    CopyMemory(data, &material->material, size);
    data->dwSize = size;
    return D3D_OK;
}

static HRESULT d3d_material_get_handle(D3DLegacyMaterial *material,
        D3D7Device *device, D3DMATERIALHANDLE *handle)
{
    if (!handle || !device || device->owner != material->owner)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!material->handle) material->handle = allocate_handle();
    *handle = material->handle;
    return D3D_OK;
}

static HRESULT WINAPI d3d_material3_GetHandle(IDirect3DMaterial3 *iface,
        IDirect3DDevice3 *device, D3DMATERIALHANDLE *handle)
{
    return d3d_material_get_handle(d3d_material_from_any(iface),
            device ? d3d3_device_from_iface(device) : NULL, handle);
}

static HRESULT WINAPI d3d_material2_GetHandle(IDirect3DMaterial2 *iface,
        IDirect3DDevice2 *device, D3DMATERIALHANDLE *handle)
{
    return d3d_material_get_handle(d3d_material_from_any(iface),
            device ? d3d2_device_from_iface(device) : NULL, handle);
}

static HRESULT WINAPI d3d_material1_Initialize(IDirect3DMaterial *iface,
        IDirect3D *d3d)
{
    D3DLegacyMaterial *material = d3d_material_from_any(iface);
    return d3d && d3d1_object_from_iface(d3d) == material->owner ?
            D3D_OK : DDERR_INVALIDPARAMS;
}

static HRESULT WINAPI d3d_material1_GetHandle(IDirect3DMaterial *iface,
        IDirect3DDevice *device, D3DMATERIALHANDLE *handle)
{
    return d3d_material_get_handle(d3d_material_from_any(iface),
            device ? d3d1_device_from_iface(device) : NULL, handle);
}

static HRESULT WINAPI d3d_material1_Reserve(IDirect3DMaterial *iface)
{
    (void)iface;
    return D3D_OK;
}

static HRESULT WINAPI d3d_material1_Unreserve(IDirect3DMaterial *iface)
{
    (void)iface;
    return D3D_OK;
}

static HRESULT d3d_legacy_create_material(DDrawObject *owner,
        D3DLegacyMaterial **out, IUnknown *outer)
{
    D3DLegacyMaterial *material;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    material = (D3DLegacyMaterial *)heap_alloc_zero(sizeof(*material));
    if (!material) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    material->vtbl3 = &g_d3d_material3_vtbl;
    material->vtbl2 = &g_d3d_material2_vtbl;
    material->vtbl1 = &g_d3d_material1_vtbl;
    material->ref = 1;
    material->owner = owner;
    material->material.dwSize = sizeof(material->material);
    material->material.diffuse.r = material->material.diffuse.g =
            material->material.diffuse.b = material->material.diffuse.a = 1.0f;
    material->next = owner->legacy_materials;
    owner->legacy_materials = material;
    ddraw_AddRef((IDirectDraw7 *)&owner->vtbl7);
    *out = material;
    return D3D_OK;
}

static D3DLegacyLight *d3d_light_from_iface(IDirect3DLight *iface)
{
    return (D3DLegacyLight *)iface;
}

static void d3d_legacy_light7(const D3DLIGHT2 *source, D3DLIGHT7 *destination)
{
    ZeroMemory(destination, sizeof(*destination));
    destination->dltType = source->dltType;
    destination->dcvDiffuse = source->dcvColor;
    if (!(source->dwFlags & D3DLIGHT_NO_SPECULAR))
        destination->dcvSpecular = source->dcvColor;
    destination->dvPosition = source->dvPosition;
    destination->dvDirection = source->dvDirection;
    destination->dvRange = source->dvRange;
    destination->dvFalloff = source->dvFalloff;
    destination->dvAttenuation0 = source->dvAttenuation0;
    destination->dvAttenuation1 = source->dvAttenuation1;
    destination->dvAttenuation2 = source->dvAttenuation2;
    destination->dvTheta = source->dvTheta;
    destination->dvPhi = source->dvPhi;
}

static HRESULT WINAPI d3d_light_QueryInterface(IDirect3DLight *iface,
        REFIID iid, void **out)
{
    if (!out) return DDWG_TRACE_ERROR(E_POINTER);
    *out = NULL;
    if (!iid_is_unknown(iid) && !guid_equal(iid, &IID_IDirect3DLight))
        return DDWG_TRACE_ERROR(E_NOINTERFACE);
    *out = iface;
    InterlockedIncrement(&d3d_light_from_iface(iface)->ref);
    return S_OK;
}

static ULONG WINAPI d3d_light_AddRef(IDirect3DLight *iface)
{
    return (ULONG)InterlockedIncrement(&d3d_light_from_iface(iface)->ref);
}

static ULONG WINAPI d3d_light_Release(IDirect3DLight *iface)
{
    D3DLegacyLight *light = d3d_light_from_iface(iface);
    LONG remaining = InterlockedDecrement(&light->ref);
    if (!remaining) {
        DDrawObject *owner = light->owner;
        heap_free(light);
        ddraw_Release((IDirectDraw7 *)&owner->vtbl7);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

static HRESULT WINAPI d3d_light_Initialize(IDirect3DLight *iface,
        IDirect3D *d3d)
{
    return d3d && d3d1_object_from_iface(d3d) ==
            d3d_light_from_iface(iface)->owner ? D3D_OK : DDERR_INVALIDPARAMS;
}

static HRESULT WINAPI d3d_light_SetLight(IDirect3DLight *iface,
        D3DLIGHT *data)
{
    D3DLegacyLight *light = d3d_light_from_iface(iface);
    DWORD size;
    if (!data || data->dwSize < sizeof(D3DLIGHT)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    size = data->dwSize < sizeof(light->light) ? data->dwSize :
            sizeof(light->light);
    ZeroMemory(&light->light, sizeof(light->light));
    CopyMemory(&light->light, data, size);
    light->light.dwSize = sizeof(light->light);
    if (light->viewport && light->viewport->device &&
            light->viewport->device->legacy_current_viewport ==
            light->viewport)
        return d3d_legacy_apply_viewport(light->viewport);
    return D3D_OK;
}

static HRESULT WINAPI d3d_light_GetLight(IDirect3DLight *iface,
        D3DLIGHT *data)
{
    D3DLegacyLight *light = d3d_light_from_iface(iface);
    DWORD size;
    if (!data || data->dwSize < sizeof(DWORD)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    size = data->dwSize < sizeof(light->light) ? data->dwSize :
            sizeof(light->light);
    CopyMemory(data, &light->light, size);
    data->dwSize = size;
    return D3D_OK;
}

static HRESULT d3d_legacy_create_light(DDrawObject *owner,
        IDirect3DLight **out, IUnknown *outer)
{
    D3DLegacyLight *light;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    light = (D3DLegacyLight *)heap_alloc_zero(sizeof(*light));
    if (!light) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    light->vtbl = &g_d3d_light_vtbl;
    light->ref = 1;
    light->owner = owner;
    light->light.dwSize = sizeof(light->light);
    light->light.dltType = D3DLIGHT_DIRECTIONAL;
    light->light.dcvColor.r = light->light.dcvColor.g =
            light->light.dcvColor.b = light->light.dcvColor.a = 1.0f;
    light->light.dvRange = 1.0e20f;
    light->light.dvAttenuation0 = 1.0f;
    light->light.dwFlags = D3DLIGHT_ACTIVE;
    ddraw_AddRef((IDirectDraw7 *)&owner->vtbl7);
    *out = (IDirect3DLight *)light;
    return D3D_OK;
}

/* ------------------------------------------------------------------ *
 * Viewports
 * ------------------------------------------------------------------ */

static void d3d_legacy_viewport7(D3DLegacyViewport *viewport,
        D3DVIEWPORT7 *viewport7)
{
    if (viewport->viewport2_set) {
        viewport7->dwX = viewport->viewport2.dwX;
        viewport7->dwY = viewport->viewport2.dwY;
        viewport7->dwWidth = viewport->viewport2.dwWidth;
        viewport7->dwHeight = viewport->viewport2.dwHeight;
        viewport7->dvMinZ = viewport->viewport2.dvMinZ;
        viewport7->dvMaxZ = viewport->viewport2.dvMaxZ;
    } else {
        viewport7->dwX = viewport->viewport1.dwX;
        viewport7->dwY = viewport->viewport1.dwY;
        viewport7->dwWidth = viewport->viewport1.dwWidth;
        viewport7->dwHeight = viewport->viewport1.dwHeight;
        viewport7->dvMinZ = viewport->viewport1.dvMinZ;
        viewport7->dvMaxZ = viewport->viewport1.dvMaxZ;
    }
}

static HRESULT d3d_legacy_apply_viewport(D3DLegacyViewport *viewport)
{
    D3DVIEWPORT7 viewport7;
    D3DLegacyLight *light;
    DWORD index = 0;
    HRESULT result;
    if (!viewport->device) return DDWG_TRACE_ERROR(D3DERR_VIEWPORTHASNODEVICE);
    if (!viewport->viewport1_set && !viewport->viewport2_set)
        return DDWG_TRACE_ERROR(D3DERR_VIEWPORTDATANOTSET);
    d3d_legacy_viewport7(viewport, &viewport7);
    result = d3d7_device_SetViewport((IDirect3DDevice7 *)viewport->device,
            &viewport7);
    if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    for (light = viewport->lights; light && index < D3D7_MAX_LIGHTS;
            light = light->next_in_viewport, ++index) {
        D3DLIGHT7 light7;
        d3d_legacy_light7(&light->light, &light7);
        result = d3d7_device_SetLight((IDirect3DDevice7 *)viewport->device,
                index, &light7);
        if (FAILED(result)) return DDWG_TRACE_ERROR(result);
        result = d3d7_device_LightEnable((IDirect3DDevice7 *)viewport->device,
                index, !!(light->light.dwFlags & D3DLIGHT_ACTIVE));
        if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    }
    while (index < D3D7_MAX_LIGHTS) {
        d3d7_device_LightEnable((IDirect3DDevice7 *)viewport->device,
                index++, FALSE);
    }
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_QueryInterface(IDirect3DViewport3 *iface,
        REFIID iid, void **out)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    if (!out) return DDWG_TRACE_ERROR(E_POINTER);
    *out = NULL;
    if (iid_is_unknown(iid) || guid_equal(iid, &IID_IDirect3DViewport3))
        *out = &viewport->vtbl3;
    else if (guid_equal(iid, &IID_IDirect3DViewport2))
        *out = &viewport->vtbl2;
    else if (guid_equal(iid, &IID_IDirect3DViewport))
        *out = &viewport->vtbl1;
    else
        return DDWG_TRACE_ERROR(E_NOINTERFACE);
    InterlockedIncrement(&viewport->ref);
    return S_OK;
}

static ULONG WINAPI d3d_viewport_AddRef(IDirect3DViewport3 *iface)
{
    return (ULONG)InterlockedIncrement(&d3d_viewport_from_any(iface)->ref);
}

static ULONG WINAPI d3d_viewport_Release(IDirect3DViewport3 *iface)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    LONG remaining = InterlockedDecrement(&viewport->ref);
    if (!remaining) {
        DDrawObject *owner = viewport->owner;
        D3DLegacyLight *light = viewport->lights;
        while (light) {
            D3DLegacyLight *next = light->next_in_viewport;
            light->viewport = NULL;
            light->next_in_viewport = NULL;
            d3d_light_Release((IDirect3DLight *)light);
            light = next;
        }
        if (viewport->background_depth)
            surface_Release((IDirectDrawSurface7 *)&
                    viewport->background_depth->vtbl7);
        heap_free(viewport);
        ddraw_Release((IDirectDraw7 *)&owner->vtbl7);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

static HRESULT WINAPI d3d_viewport_Initialize(IDirect3DViewport3 *iface,
        IDirect3D *d3d)
{
    return d3d && d3d1_object_from_iface(d3d) ==
            d3d_viewport_from_any(iface)->owner ? D3D_OK : DDERR_INVALIDPARAMS;
}

static HRESULT WINAPI d3d_viewport_GetViewport(IDirect3DViewport3 *iface,
        D3DVIEWPORT *data)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    if (!data || data->dwSize < sizeof(D3DVIEWPORT))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!viewport->viewport1_set) return DDWG_TRACE_ERROR(D3DERR_VIEWPORTDATANOTSET);
    *data = viewport->viewport1;
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_SetViewport(IDirect3DViewport3 *iface,
        D3DVIEWPORT *data)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    if (!data || data->dwSize < sizeof(D3DVIEWPORT) || !data->dwWidth ||
            !data->dwHeight || data->dvMinZ > data->dvMaxZ)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    viewport->viewport1 = *data;
    viewport->viewport1.dwSize = sizeof(viewport->viewport1);
    viewport->viewport1_set = TRUE;
    viewport->viewport2_set = FALSE;
    if (viewport->device && viewport->device->legacy_current_viewport == viewport)
        return d3d_legacy_apply_viewport(viewport);
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_GetViewport2(IDirect3DViewport3 *iface,
        D3DVIEWPORT2 *data)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    if (!data || data->dwSize < sizeof(D3DVIEWPORT2))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!viewport->viewport2_set) return DDWG_TRACE_ERROR(D3DERR_VIEWPORTDATANOTSET);
    *data = viewport->viewport2;
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_SetViewport2(IDirect3DViewport3 *iface,
        D3DVIEWPORT2 *data)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    if (!data || data->dwSize < sizeof(D3DVIEWPORT2) || !data->dwWidth ||
            !data->dwHeight || data->dvMinZ > data->dvMaxZ)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    viewport->viewport2 = *data;
    viewport->viewport2.dwSize = sizeof(viewport->viewport2);
    viewport->viewport2_set = TRUE;
    viewport->viewport1_set = FALSE;
    if (viewport->device && viewport->device->legacy_current_viewport == viewport)
        return d3d_legacy_apply_viewport(viewport);
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_TransformVertices(
        IDirect3DViewport3 *iface, DWORD count, D3DTRANSFORMDATA *data,
        DWORD flags, DWORD *offscreen)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    D3D7Device *device = viewport->device;
    D3DMATRIX world_view, transform;
    D3DVIEWPORT7 viewport7;
    DWORD index;
    DWORD clip_union = 0, clip_intersection = 0x3fu;
    if (!device) return DDWG_TRACE_ERROR(D3DERR_VIEWPORTHASNODEVICE);
    if (!data || data->dwSize < sizeof(*data) || !data->lpIn ||
            !data->lpOut || data->dwInSize < sizeof(D3DVECTOR) ||
            data->dwOutSize < sizeof(D3DTLVERTEX) ||
            !(flags & (D3DTRANSFORM_CLIPPED | D3DTRANSFORM_UNCLIPPED)))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!viewport->viewport1_set && !viewport->viewport2_set)
        return DDWG_TRACE_ERROR(D3DERR_VIEWPORTDATANOTSET);
    d3d7_multiply_matrix(&device->transforms[D3DTRANSFORMSTATE_WORLD],
            &device->transforms[D3DTRANSFORMSTATE_VIEW], &world_view);
    d3d7_multiply_matrix(&world_view,
            &device->transforms[D3DTRANSFORMSTATE_PROJECTION], &transform);
    d3d_legacy_viewport7(viewport, &viewport7);
    for (index = 0; index < count; ++index) {
        const D3DVECTOR *input = (const D3DVECTOR *)((BYTE *)data->lpIn +
                index * data->dwInSize);
        D3DTLVERTEX *output = (D3DTLVERTEX *)((BYTE *)data->lpOut +
                index * data->dwOutSize);
        float source[4] = { input->x, input->y, input->z, 1.0f };
        float clip[4];
        float reciprocal_w;
        DWORD vertex_clip = 0;
        d3d7_transform_vector(&transform, source, clip);
        if (clip[0] < -clip[3]) vertex_clip |= D3DCLIP_LEFT;
        if (clip[0] > clip[3]) vertex_clip |= D3DCLIP_RIGHT;
        if (clip[1] > clip[3]) vertex_clip |= D3DCLIP_TOP;
        if (clip[1] < -clip[3]) vertex_clip |= D3DCLIP_BOTTOM;
        if (clip[2] < 0.0f) vertex_clip |= D3DCLIP_FRONT;
        if (clip[2] > clip[3]) vertex_clip |= D3DCLIP_BACK;
        clip_union |= vertex_clip;
        clip_intersection &= vertex_clip;
        reciprocal_w = clip[3] != 0.0f ? 1.0f / clip[3] : 0.0f;
        output->sx = viewport7.dwX + (clip[0] * reciprocal_w + 1.0f) *
                (float)viewport7.dwWidth * 0.5f;
        output->sy = viewport7.dwY + (1.0f - clip[1] * reciprocal_w) *
                (float)viewport7.dwHeight * 0.5f;
        output->sz = viewport7.dvMinZ + clip[2] * reciprocal_w *
                (viewport7.dvMaxZ - viewport7.dvMinZ);
        output->rhw = reciprocal_w;
        if (data->lpHOut) {
            data->lpHOut[index].dwFlags = vertex_clip;
            data->lpHOut[index].hx = clip[0];
            data->lpHOut[index].hy = clip[1];
            data->lpHOut[index].hz = clip[2];
        }
    }
    data->dwClipUnion = clip_union;
    data->dwClipIntersection = count ? clip_intersection : 0;
    if (offscreen) *offscreen = clip_intersection ? TRUE : FALSE;
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_LightElements(IDirect3DViewport3 *iface,
        DWORD count, D3DLIGHTDATA *data)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    DWORD index;
    if (!viewport->device) return DDWG_TRACE_ERROR(D3DERR_VIEWPORTHASNODEVICE);
    if (!data || data->dwSize < sizeof(*data) || !data->lpIn || !data->lpOut ||
            data->dwInSize < sizeof(D3DLIGHTINGELEMENT) ||
            data->dwOutSize < sizeof(D3DTLVERTEX))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    for (index = 0; index < count; ++index) {
        D3DLIGHTINGELEMENT *input = (D3DLIGHTINGELEMENT *)((BYTE *)data->lpIn +
                index * data->dwInSize);
        D3DTLVERTEX *output = (D3DTLVERTEX *)((BYTE *)data->lpOut +
                index * data->dwOutSize);
        float position[4] = { input->dvPosition.x, input->dvPosition.y,
                input->dvPosition.z, 1.0f };
        float color[4];
        d3d7_light_vertex(viewport->device, position,
                &input->dvNormal.x, color);
        output->color = d3d7_pack_color(color);
        output->specular = 0;
    }
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_SetBackground(IDirect3DViewport3 *iface,
        D3DMATERIALHANDLE handle)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    if (handle && !d3d_legacy_find_material(viewport->owner, handle))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    viewport->background = handle;
    viewport->background_valid = !!handle;
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_GetBackground(IDirect3DViewport3 *iface,
        D3DMATERIALHANDLE *handle, WINBOOL *valid)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    if (!handle || !valid) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *handle = viewport->background;
    *valid = viewport->background_valid;
    return D3D_OK;
}

static HRESULT d3d_viewport_set_depth(D3DLegacyViewport *viewport,
        void *surface_iface)
{
    DDSurface *surface = surface_iface ? surface_from_any(surface_iface, NULL) :
            NULL;
    if (surface && (surface->owner != viewport->owner ||
            !format_is_depth(surface->format))) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (surface) surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
    if (viewport->background_depth)
        surface_Release((IDirectDrawSurface7 *)&
                viewport->background_depth->vtbl7);
    viewport->background_depth = surface;
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_SetBackgroundDepth(
        IDirect3DViewport3 *iface, IDirectDrawSurface *surface)
{
    return d3d_viewport_set_depth(d3d_viewport_from_any(iface), surface);
}

static HRESULT WINAPI d3d_viewport_SetBackgroundDepth2(
        IDirect3DViewport3 *iface, IDirectDrawSurface4 *surface)
{
    return d3d_viewport_set_depth(d3d_viewport_from_any(iface), surface);
}

static HRESULT d3d_viewport_get_depth(D3DLegacyViewport *viewport,
        void **out, WINBOOL *valid, unsigned version)
{
    if (!out || !valid) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *valid = !!viewport->background_depth;
    *out = surface_interface(viewport->background_depth, version);
    if (viewport->background_depth)
        surface_AddRef((IDirectDrawSurface7 *)&
                viewport->background_depth->vtbl7);
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_GetBackgroundDepth(
        IDirect3DViewport3 *iface, IDirectDrawSurface **out, WINBOOL *valid)
{
    return d3d_viewport_get_depth(d3d_viewport_from_any(iface), (void **)out,
            valid, DD_VERSION_1);
}

static HRESULT WINAPI d3d_viewport_GetBackgroundDepth2(
        IDirect3DViewport3 *iface, IDirectDrawSurface4 **out, WINBOOL *valid)
{
    return d3d_viewport_get_depth(d3d_viewport_from_any(iface), (void **)out,
            valid, DD_VERSION_4);
}

static HRESULT WINAPI d3d_viewport_Clear2(IDirect3DViewport3 *iface,
        DWORD count, D3DRECT *rects, DWORD flags, DWORD color, D3DVALUE z,
        DWORD stencil)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    if (!viewport->device) return DDWG_TRACE_ERROR(D3DERR_VIEWPORTHASNODEVICE);
    return d3d7_device_Clear((IDirect3DDevice7 *)viewport->device, count,
            rects, flags, color, z, stencil);
}

static HRESULT WINAPI d3d_viewport_Clear(IDirect3DViewport3 *iface,
        DWORD count, D3DRECT *rects, DWORD flags)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    D3DLegacyMaterial *material = d3d_legacy_find_material(viewport->owner,
            viewport->background);
    DWORD color = material ? D3DRGBA(material->material.diffuse.r,
            material->material.diffuse.g, material->material.diffuse.b,
            material->material.diffuse.a) : 0;
    return d3d_viewport_Clear2(iface, count, rects, flags, color, 1.0f, 0);
}

static HRESULT WINAPI d3d_viewport_AddLight(IDirect3DViewport3 *iface,
        IDirect3DLight *light_iface)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    D3DLegacyLight *light;
    if (!light_iface || light_iface->lpVtbl != &g_d3d_light_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    light = d3d_light_from_iface(light_iface);
    if (light->owner != viewport->owner) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (light->viewport == viewport) return D3D_OK;
    if (light->viewport) return DDWG_TRACE_ERROR(D3DERR_LIGHTHASVIEWPORT);
    light->viewport = viewport;
    light->next_in_viewport = viewport->lights;
    viewport->lights = light;
    d3d_light_AddRef(light_iface);
    if (viewport->device && viewport->device->legacy_current_viewport == viewport)
        return d3d_legacy_apply_viewport(viewport);
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_DeleteLight(IDirect3DViewport3 *iface,
        IDirect3DLight *light_iface)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    D3DLegacyLight *light;
    D3DLegacyLight **link;
    if (!light_iface || light_iface->lpVtbl != &g_d3d_light_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    light = d3d_light_from_iface(light_iface);
    if (light->viewport != viewport) return DDWG_TRACE_ERROR(D3DERR_LIGHTNOTINTHISVIEWPORT);
    link = &viewport->lights;
    while (*link && *link != light) link = &(*link)->next_in_viewport;
    if (*link) *link = light->next_in_viewport;
    light->viewport = NULL;
    light->next_in_viewport = NULL;
    if (viewport->device && viewport->device->legacy_current_viewport == viewport)
        d3d_legacy_apply_viewport(viewport);
    d3d_light_Release(light_iface);
    return D3D_OK;
}

static HRESULT WINAPI d3d_viewport_NextLight(IDirect3DViewport3 *iface,
        IDirect3DLight *reference, IDirect3DLight **out, DWORD flags)
{
    D3DLegacyViewport *viewport = d3d_viewport_from_any(iface);
    D3DLegacyLight *light = NULL;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (flags == D3DNEXT_HEAD) light = viewport->lights;
    else if (flags == D3DNEXT_TAIL) {
        for (light = viewport->lights; light && light->next_in_viewport;
                light = light->next_in_viewport) {}
    } else if (flags == D3DNEXT_NEXT && reference &&
            reference->lpVtbl == &g_d3d_light_vtbl) {
        light = d3d_light_from_iface(reference);
        if (light->viewport != viewport) return DDWG_TRACE_ERROR(D3DERR_LIGHTNOTINTHISVIEWPORT);
        light = light->next_in_viewport;
    } else return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!light) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
    *out = (IDirect3DLight *)light;
    d3d_light_AddRef(*out);
    return D3D_OK;
}

static HRESULT d3d_legacy_create_viewport(DDrawObject *owner,
        D3DLegacyViewport **out, IUnknown *outer)
{
    D3DLegacyViewport *viewport;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    viewport = (D3DLegacyViewport *)heap_alloc_zero(sizeof(*viewport));
    if (!viewport) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    viewport->vtbl3 = &g_d3d_viewport3_vtbl;
    viewport->vtbl2 = &g_d3d_viewport2_vtbl;
    viewport->vtbl1 = &g_d3d_viewport1_vtbl;
    viewport->ref = 1;
    viewport->owner = owner;
    viewport->viewport1.dwSize = sizeof(viewport->viewport1);
    viewport->viewport2.dwSize = sizeof(viewport->viewport2);
    ddraw_AddRef((IDirectDraw7 *)&owner->vtbl7);
    *out = viewport;
    return D3D_OK;
}

static HRESULT WINAPI d3d3_CreateLight(IDirect3D3 *iface,
        IDirect3DLight **out, IUnknown *outer)
{
    return d3d_legacy_create_light(d3d3_object_from_iface(iface), out, outer);
}

static HRESULT WINAPI d3d2_CreateLight(IDirect3D2 *iface,
        IDirect3DLight **out, IUnknown *outer)
{
    return d3d_legacy_create_light(d3d2_object_from_iface(iface), out, outer);
}

static HRESULT WINAPI d3d1_CreateLight(IDirect3D *iface,
        IDirect3DLight **out, IUnknown *outer)
{
    return d3d_legacy_create_light(d3d1_object_from_iface(iface), out, outer);
}

static HRESULT WINAPI d3d3_CreateMaterial(IDirect3D3 *iface,
        IDirect3DMaterial3 **out, IUnknown *outer)
{
    D3DLegacyMaterial *material;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    HRESULT result = d3d_legacy_create_material(d3d3_object_from_iface(iface),
            &material, outer);
    if (out) *out = SUCCEEDED(result) ?
            (IDirect3DMaterial3 *)&material->vtbl3 : NULL;
    return DDWG_TRACE_ERROR(result);
}

static HRESULT WINAPI d3d2_CreateMaterial(IDirect3D2 *iface,
        IDirect3DMaterial2 **out, IUnknown *outer)
{
    D3DLegacyMaterial *material;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    HRESULT result = d3d_legacy_create_material(d3d2_object_from_iface(iface),
            &material, outer);
    if (out) *out = SUCCEEDED(result) ?
            (IDirect3DMaterial2 *)&material->vtbl2 : NULL;
    return DDWG_TRACE_ERROR(result);
}

static HRESULT WINAPI d3d1_CreateMaterial(IDirect3D *iface,
        IDirect3DMaterial **out, IUnknown *outer)
{
    D3DLegacyMaterial *material;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    HRESULT result = d3d_legacy_create_material(d3d1_object_from_iface(iface),
            &material, outer);
    if (out) *out = SUCCEEDED(result) ?
            (IDirect3DMaterial *)&material->vtbl1 : NULL;
    return DDWG_TRACE_ERROR(result);
}

static HRESULT WINAPI d3d3_CreateViewport(IDirect3D3 *iface,
        IDirect3DViewport3 **out, IUnknown *outer)
{
    D3DLegacyViewport *viewport;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    HRESULT result = d3d_legacy_create_viewport(d3d3_object_from_iface(iface),
            &viewport, outer);
    if (out) *out = SUCCEEDED(result) ?
            (IDirect3DViewport3 *)&viewport->vtbl3 : NULL;
    return DDWG_TRACE_ERROR(result);
}

static HRESULT WINAPI d3d2_CreateViewport(IDirect3D2 *iface,
        IDirect3DViewport2 **out, IUnknown *outer)
{
    D3DLegacyViewport *viewport;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    HRESULT result = d3d_legacy_create_viewport(d3d2_object_from_iface(iface),
            &viewport, outer);
    if (out) *out = SUCCEEDED(result) ?
            (IDirect3DViewport2 *)&viewport->vtbl2 : NULL;
    return DDWG_TRACE_ERROR(result);
}

static HRESULT WINAPI d3d1_CreateViewport(IDirect3D *iface,
        IDirect3DViewport **out, IUnknown *outer)
{
    D3DLegacyViewport *viewport;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    HRESULT result = d3d_legacy_create_viewport(d3d1_object_from_iface(iface),
            &viewport, outer);
    if (out) *out = SUCCEEDED(result) ?
            (IDirect3DViewport *)&viewport->vtbl1 : NULL;
    return DDWG_TRACE_ERROR(result);
}

/* ------------------------------------------------------------------ *
 * Legacy texture views on DirectDraw surfaces
 * ------------------------------------------------------------------ */

static HRESULT WINAPI d3d_texture2_QueryInterface(IDirect3DTexture2 *iface,
        REFIID iid, void **out)
{
    DDSurface *surface = d3d_texture2_from_iface(iface);
    return surface_QueryInterface((IDirectDrawSurface7 *)&surface->vtbl7,
            iid, out);
}

static ULONG WINAPI d3d_texture2_AddRef(IDirect3DTexture2 *iface)
{
    return surface_AddRef((IDirectDrawSurface7 *)&
            d3d_texture2_from_iface(iface)->vtbl7);
}

static ULONG WINAPI d3d_texture2_Release(IDirect3DTexture2 *iface)
{
    return surface_Release((IDirectDrawSurface7 *)&
            d3d_texture2_from_iface(iface)->vtbl7);
}

static HRESULT d3d_texture_get_handle(DDSurface *surface, D3D7Device *device,
        D3DTEXTUREHANDLE *handle)
{
    if (!handle || !device || device->owner != surface->owner ||
            format_is_depth(surface->format)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!surface->texture_handle) surface->texture_handle = allocate_handle();
    *handle = surface->texture_handle;
    return D3D_OK;
}

static HRESULT WINAPI d3d_texture2_GetHandle(IDirect3DTexture2 *iface,
        IDirect3DDevice2 *device, D3DTEXTUREHANDLE *handle)
{
    return d3d_texture_get_handle(d3d_texture2_from_iface(iface),
            device ? d3d2_device_from_iface(device) : NULL, handle);
}

static HRESULT WINAPI d3d_texture2_PaletteChanged(IDirect3DTexture2 *iface,
        DWORD start, DWORD count)
{
    DDSurface *surface = d3d_texture2_from_iface(iface);
    (void)start;
    (void)count;
    if (surface->palette) emit_palette_table(surface->palette);
    return D3D_OK;
}

static HRESULT d3d_legacy_texture_load_surface(DDSurface *destination,
        DDSurface *source)
{
    DDSurface *dst_level = destination;
    DDSurface *src_level = source;
    UINT face;
    HRESULT result;
    if (!destination || !source || destination->owner != source->owner ||
            destination->format != source->format ||
            destination->width != source->width ||
            destination->height != source->height)
        return DDWG_TRACE_ERROR(D3DERR_TEXTURE_BADSIZE);
    while (dst_level && src_level) {
        result = surface_Blt((IDirectDrawSurface7 *)&dst_level->vtbl7, NULL,
                (IDirectDrawSurface7 *)&src_level->vtbl7, NULL, DDBLT_WAIT,
                NULL);
        if (FAILED(result)) return DDWG_TRACE_ERROR(D3DERR_TEXTURE_LOAD_FAILED);
        dst_level = dst_level->mip_next;
        src_level = src_level->mip_next;
    }
    if (dst_level || src_level) return DDWG_TRACE_ERROR(D3DERR_TEXTURE_BADSIZE);
    for (face = 1; face < 6u; ++face) {
        if (!!destination->cube_faces[face] != !!source->cube_faces[face])
            return DDWG_TRACE_ERROR(D3DERR_TEXTURE_BADSIZE);
        if (destination->cube_faces[face]) {
            result = d3d_legacy_texture_load_surface(
                    destination->cube_faces[face], source->cube_faces[face]);
            if (FAILED(result)) return DDWG_TRACE_ERROR(result);
        }
    }
    return D3D_OK;
}

static HRESULT WINAPI d3d_texture2_Load(IDirect3DTexture2 *iface,
        IDirect3DTexture2 *source_iface)
{
    if (!source_iface || source_iface->lpVtbl != &g_d3d_texture2_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    return d3d_legacy_texture_load_surface(d3d_texture2_from_iface(iface),
            d3d_texture2_from_iface(source_iface));
}

static HRESULT WINAPI d3d_texture1_QueryInterface(IDirect3DTexture *iface,
        REFIID iid, void **out)
{
    DDSurface *surface = d3d_texture1_from_iface(iface);
    return surface_QueryInterface((IDirectDrawSurface7 *)&surface->vtbl7,
            iid, out);
}

static ULONG WINAPI d3d_texture1_AddRef(IDirect3DTexture *iface)
{
    return surface_AddRef((IDirectDrawSurface7 *)&
            d3d_texture1_from_iface(iface)->vtbl7);
}

static ULONG WINAPI d3d_texture1_Release(IDirect3DTexture *iface)
{
    return surface_Release((IDirectDrawSurface7 *)&
            d3d_texture1_from_iface(iface)->vtbl7);
}

static HRESULT WINAPI d3d_texture1_Initialize(IDirect3DTexture *iface,
        IDirect3DDevice *device, IDirectDrawSurface *surface_iface)
{
    DDSurface *surface = d3d_texture1_from_iface(iface);
    if (!device || !surface_iface || surface_from_any(surface_iface, NULL) !=
            surface || d3d1_device_from_iface(device)->owner != surface->owner)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    return DDWG_TRACE_ERROR(DDERR_ALREADYINITIALIZED);
}

static HRESULT WINAPI d3d_texture1_GetHandle(IDirect3DTexture *iface,
        IDirect3DDevice *device, D3DTEXTUREHANDLE *handle)
{
    return d3d_texture_get_handle(d3d_texture1_from_iface(iface),
            device ? d3d1_device_from_iface(device) : NULL, handle);
}

static HRESULT WINAPI d3d_texture1_PaletteChanged(IDirect3DTexture *iface,
        DWORD start, DWORD count)
{
    DDSurface *surface = d3d_texture1_from_iface(iface);
    (void)start;
    (void)count;
    if (surface->palette) emit_palette_table(surface->palette);
    return D3D_OK;
}

static HRESULT WINAPI d3d_texture1_Load(IDirect3DTexture *iface,
        IDirect3DTexture *source_iface)
{
    if (!source_iface || source_iface->lpVtbl != &g_d3d_texture1_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    return d3d_legacy_texture_load_surface(d3d_texture1_from_iface(iface),
            d3d_texture1_from_iface(source_iface));
}

static HRESULT WINAPI d3d_texture1_Unload(IDirect3DTexture *iface)
{
    (void)iface;
    /* The guest shadow is canonical; eviction never loses texture contents. */
    return D3D_OK;
}

/* ------------------------------------------------------------------ *
 * Legacy device creation and common immediate-mode methods
 * ------------------------------------------------------------------ */

static HRESULT d3d_legacy_create_device(DDrawObject *owner, REFCLSID clsid,
        void *surface_iface, unsigned version, void **out)
{
    DDSurface *surface;
    IDirect3DDevice7 *device7;
    const GUID *normalized_class = clsid;
    HRESULT result;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (!surface_iface) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    surface = surface_from_any(surface_iface, NULL);
    if (!surface || surface->owner != owner) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (guid_equal(clsid, &IID_IDirect3DRampDevice) ||
            guid_equal(clsid, &IID_IDirect3DMMXDevice) ||
            guid_equal(clsid, &IID_IDirect3DRefDevice))
        normalized_class = &IID_IDirect3DRGBDevice;
    result = d3d7_CreateDevice((IDirect3D7 *)&owner->d3d7_vtbl,
            normalized_class,
            (IDirectDrawSurface7 *)&surface->vtbl7, &device7);
    if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    if (version == 3u)
        *out = &d3d7_device_from_iface(device7)->device3_vtbl;
    else if (version == 2u)
        *out = &d3d7_device_from_iface(device7)->device2_vtbl;
    else
        *out = &d3d7_device_from_iface(device7)->device1_vtbl;
    return D3D_OK;
}

static HRESULT WINAPI d3d3_CreateDevice(IDirect3D3 *iface, REFCLSID clsid,
        IDirectDrawSurface4 *surface, IDirect3DDevice3 **out, IUnknown *outer)
{
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    return d3d_legacy_create_device(d3d3_object_from_iface(iface), clsid,
            surface, 3u, (void **)out);
}

static HRESULT WINAPI d3d2_CreateDevice(IDirect3D2 *iface, REFCLSID clsid,
        IDirectDrawSurface *surface, IDirect3DDevice2 **out)
{
    return d3d_legacy_create_device(d3d2_object_from_iface(iface), clsid,
            surface, 2u, (void **)out);
}

static HRESULT WINAPI d3d3_CreateVertexBuffer(IDirect3D3 *iface,
        D3DVERTEXBUFFERDESC *description, IDirect3DVertexBuffer **out,
        DWORD flags, IUnknown *outer)
{
    IDirect3DVertexBuffer7 *buffer7;
    HRESULT result;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    result = d3d7_CreateVertexBuffer((IDirect3D7 *)&
            d3d3_object_from_iface(iface)->d3d7_vtbl, description, &buffer7,
            flags);
    if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    *out = (IDirect3DVertexBuffer *)&
            d3d7_vb_from_iface(buffer7)->legacy_vtbl;
    return D3D_OK;
}

static HRESULT WINAPI d3d3_EnumZBufferFormats(IDirect3D3 *iface,
        REFCLSID device_iid, LPD3DENUMPIXELFORMATSCALLBACK callback,
        void *context)
{
    return d3d7_EnumZBufferFormats((IDirect3D7 *)&
            d3d3_object_from_iface(iface)->d3d7_vtbl, device_iid, callback,
            context);
}

static HRESULT WINAPI d3d3_EvictManagedTextures(IDirect3D3 *iface)
{
    return d3d7_EvictManagedTextures((IDirect3D7 *)&
            d3d3_object_from_iface(iface)->d3d7_vtbl);
}

static HRESULT WINAPI d3d3_device_QueryInterface(IDirect3DDevice3 *iface,
        REFIID iid, void **out)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    if (!out) return DDWG_TRACE_ERROR(E_POINTER);
    *out = NULL;
    if (iid_is_unknown(iid) || guid_equal(iid, &IID_IDirect3DDevice7))
        *out = &device->vtbl;
    else if (guid_equal(iid, &IID_IDirect3DDevice3))
        *out = &device->device3_vtbl;
    else if (guid_equal(iid, &IID_IDirect3DDevice2))
        *out = &device->device2_vtbl;
    else if (guid_equal(iid, &IID_IDirect3DDevice))
        *out = &device->device1_vtbl;
    else
        return DDWG_TRACE_ERROR(E_NOINTERFACE);
    d3d7_device_AddRef((IDirect3DDevice7 *)device);
    return S_OK;
}

static ULONG WINAPI d3d3_device_AddRef(IDirect3DDevice3 *iface)
{
    return d3d7_device_AddRef((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface));
}

static ULONG WINAPI d3d3_device_Release(IDirect3DDevice3 *iface)
{
    return d3d7_device_Release((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface));
}

static HRESULT d3d_legacy_device_get_caps(D3DDEVICEDESC *hal,
        D3DDEVICEDESC *hel)
{
    if (!hal && !hel) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (hal) d3d_legacy_fill_caps(hal);
    if (hel) d3d_legacy_fill_caps(hel);
    return D3D_OK;
}

static HRESULT WINAPI d3d3_device_GetCaps(IDirect3DDevice3 *iface,
        D3DDEVICEDESC *hal, D3DDEVICEDESC *hel)
{
    (void)iface;
    return d3d_legacy_device_get_caps(hal, hel);
}

static HRESULT WINAPI d3d3_device_GetStats(IDirect3DDevice3 *iface,
        D3DSTATS *stats)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    DWORD size;
    if (!stats || stats->dwSize < sizeof(DWORD)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    size = stats->dwSize < sizeof(device->legacy_stats) ? stats->dwSize :
            sizeof(device->legacy_stats);
    CopyMemory(stats, &device->legacy_stats, size);
    stats->dwSize = size;
    return D3D_OK;
}

static HRESULT d3d_legacy_add_viewport(D3D7Device *device,
        D3DLegacyViewport *viewport)
{
    if (!viewport || viewport->owner != device->owner)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (viewport->device == device) return D3D_OK;
    if (viewport->device) return DDWG_TRACE_ERROR(D3DERR_VIEWPORTHASNODEVICE);
    viewport->device = device;
    viewport->next_in_device = device->legacy_viewports;
    device->legacy_viewports = viewport;
    d3d_viewport_AddRef((IDirect3DViewport3 *)&viewport->vtbl3);
    return D3D_OK;
}

static HRESULT d3d_legacy_delete_viewport(D3D7Device *device,
        D3DLegacyViewport *viewport)
{
    D3DLegacyViewport **link;
    if (!viewport || viewport->device != device)
        return DDWG_TRACE_ERROR(D3DERR_VIEWPORTHASNODEVICE);
    link = &device->legacy_viewports;
    while (*link && *link != viewport) link = &(*link)->next_in_device;
    if (*link) *link = viewport->next_in_device;
    if (device->legacy_current_viewport == viewport)
        device->legacy_current_viewport = NULL;
    viewport->device = NULL;
    viewport->next_in_device = NULL;
    d3d_viewport_Release((IDirect3DViewport3 *)&viewport->vtbl3);
    return D3D_OK;
}

static D3DLegacyViewport *d3d_legacy_next_viewport(D3D7Device *device,
        D3DLegacyViewport *reference, DWORD flags)
{
    D3DLegacyViewport *viewport;
    if (flags == D3DNEXT_HEAD) return device->legacy_viewports;
    if (flags == D3DNEXT_NEXT)
        return reference && reference->device == device ?
                reference->next_in_device : NULL;
    if (flags != D3DNEXT_TAIL) return NULL;
    for (viewport = device->legacy_viewports;
            viewport && viewport->next_in_device;
            viewport = viewport->next_in_device) {}
    return viewport;
}

static HRESULT WINAPI d3d3_device_AddViewport(IDirect3DDevice3 *iface,
        IDirect3DViewport3 *viewport)
{
    return d3d_legacy_add_viewport(d3d3_device_from_iface(iface),
            viewport ? d3d_viewport_from_any(viewport) : NULL);
}

static HRESULT WINAPI d3d3_device_DeleteViewport(IDirect3DDevice3 *iface,
        IDirect3DViewport3 *viewport)
{
    return d3d_legacy_delete_viewport(d3d3_device_from_iface(iface),
            viewport ? d3d_viewport_from_any(viewport) : NULL);
}

static HRESULT WINAPI d3d3_device_NextViewport(IDirect3DDevice3 *iface,
        IDirect3DViewport3 *reference, IDirect3DViewport3 **out, DWORD flags)
{
    D3DLegacyViewport *viewport;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    viewport = d3d_legacy_next_viewport(d3d3_device_from_iface(iface),
            reference ? d3d_viewport_from_any(reference) : NULL, flags);
    if (!viewport) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
    *out = (IDirect3DViewport3 *)&viewport->vtbl3;
    d3d_viewport_AddRef(*out);
    return D3D_OK;
}

typedef struct D3DLegacyTextureEnumContext {
    LPD3DENUMTEXTUREFORMATSCALLBACK callback;
    void *context;
} D3DLegacyTextureEnumContext;

static HRESULT CALLBACK d3d_legacy_texture_enum_callback(DDPIXELFORMAT *format,
        void *opaque)
{
    D3DLegacyTextureEnumContext *context =
            (D3DLegacyTextureEnumContext *)opaque;
    DDSURFACEDESC description;
    ZeroMemory(&description, sizeof(description));
    description.dwSize = sizeof(description);
    description.dwFlags = DDSD_CAPS | DDSD_PIXELFORMAT;
    description.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    description.ddpfPixelFormat = *format;
    return context->callback(&description, context->context);
}

static HRESULT WINAPI d3d3_device_EnumTextureFormats(
        IDirect3DDevice3 *iface, LPD3DENUMPIXELFORMATSCALLBACK callback,
        void *context)
{
    return d3d7_device_EnumTextureFormats((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), callback, context);
}

static HRESULT WINAPI d3d3_device_BeginScene(IDirect3DDevice3 *iface)
{
    return d3d7_device_BeginScene((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface));
}

static HRESULT WINAPI d3d3_device_EndScene(IDirect3DDevice3 *iface)
{
    return d3d7_device_EndScene((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface));
}

static HRESULT WINAPI d3d3_device_GetDirect3D(IDirect3DDevice3 *iface,
        IDirect3D3 **out)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = (IDirect3D3 *)&device->owner->d3d3_vtbl;
    d3d3_AddRef(*out);
    return D3D_OK;
}

static HRESULT d3d_legacy_set_current_viewport(D3D7Device *device,
        D3DLegacyViewport *viewport)
{
    if (!viewport || viewport->device != device)
        return DDWG_TRACE_ERROR(D3DERR_VIEWPORTHASNODEVICE);
    device->legacy_current_viewport = viewport;
    return d3d_legacy_apply_viewport(viewport);
}

static HRESULT WINAPI d3d3_device_SetCurrentViewport(IDirect3DDevice3 *iface,
        IDirect3DViewport3 *viewport)
{
    return d3d_legacy_set_current_viewport(d3d3_device_from_iface(iface),
            viewport ? d3d_viewport_from_any(viewport) : NULL);
}

static HRESULT WINAPI d3d3_device_GetCurrentViewport(IDirect3DDevice3 *iface,
        IDirect3DViewport3 **out)
{
    D3DLegacyViewport *viewport =
            d3d3_device_from_iface(iface)->legacy_current_viewport;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = viewport ? (IDirect3DViewport3 *)&viewport->vtbl3 : NULL;
    if (!viewport) return DDWG_TRACE_ERROR(D3DERR_NOCURRENTVIEWPORT);
    d3d_viewport_AddRef(*out);
    return D3D_OK;
}

static HRESULT WINAPI d3d3_device_SetRenderTarget(IDirect3DDevice3 *iface,
        IDirectDrawSurface4 *surface, DWORD flags)
{
    DDSurface *target = surface ? surface_from_any(surface, NULL) : NULL;
    return d3d7_device_SetRenderTarget((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), target ?
            (IDirectDrawSurface7 *)&target->vtbl7 : NULL, flags);
}

static HRESULT WINAPI d3d3_device_GetRenderTarget(IDirect3DDevice3 *iface,
        IDirectDrawSurface4 **out)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = device->render_target ? (IDirectDrawSurface4 *)&
            device->render_target->vtbl4 : NULL;
    if (device->render_target)
        surface_AddRef((IDirectDrawSurface7 *)&device->render_target->vtbl7);
    return DDWG_TRACE_ERROR(device->render_target ? D3D_OK : DDERR_NOTFOUND);
}

static BOOL d3d_legacy_grow(void **memory, DWORD *capacity, DWORD required,
        SIZE_T element_size)
{
    DWORD new_capacity;
    SIZE_T bytes;
    void *replacement;
    if (required <= *capacity) return TRUE;
    new_capacity = *capacity ? *capacity : 16u;
    while (new_capacity < required) {
        if (new_capacity > 0x7fffffffu) return FALSE;
        new_capacity *= 2u;
    }
    bytes = (SIZE_T)new_capacity * element_size;
    if (new_capacity && bytes / new_capacity != element_size) return FALSE;
    replacement = *memory ? HeapReAlloc(GetProcessHeap(), 0, *memory, bytes) :
            HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!replacement) return FALSE;
    *memory = replacement;
    *capacity = new_capacity;
    return TRUE;
}

static HRESULT d3d_legacy_begin(D3D7Device *device,
        D3DPRIMITIVETYPE primitive_type, DWORD fvf, void *vertices,
        DWORD vertex_count)
{
    D3D7FVFLayout layout;
    D9WGVertexElement elements[D3D7_MAX_VERTEX_ELEMENTS];
    UINT element_count = 0;
    if (device->legacy_in_begin) return DDWG_TRACE_ERROR(D3DERR_INBEGIN);
    if (!d3d7_decode_fvf(fvf, &layout, elements, &element_count))
        return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXTYPE);
    if (vertex_count && !vertices) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (device->legacy_vertex_stride != layout.stride) {
        heap_free(device->legacy_vertices);
        device->legacy_vertices = NULL;
        device->legacy_vertex_capacity = 0;
        device->legacy_vertex_stride = layout.stride;
    }
    if (vertex_count && !d3d_legacy_grow((void **)&device->legacy_vertices,
            &device->legacy_vertex_capacity, vertex_count, layout.stride))
        return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    if (vertex_count)
        CopyMemory(device->legacy_vertices, vertices,
                (SIZE_T)vertex_count * layout.stride);
    device->legacy_primitive_type = primitive_type;
    device->legacy_fvf = fvf;
    device->legacy_vertex_count = vertex_count;
    device->legacy_index_count = 0;
    device->legacy_in_begin = TRUE;
    return D3D_OK;
}

static HRESULT WINAPI d3d3_device_Begin(IDirect3DDevice3 *iface,
        D3DPRIMITIVETYPE primitive_type, DWORD fvf, DWORD flags)
{
    (void)flags;
    return d3d_legacy_begin(d3d3_device_from_iface(iface), primitive_type,
            fvf, NULL, 0);
}

static HRESULT WINAPI d3d3_device_BeginIndexed(IDirect3DDevice3 *iface,
        D3DPRIMITIVETYPE primitive_type, DWORD fvf, void *vertices,
        DWORD vertex_count, DWORD flags)
{
    (void)flags;
    return d3d_legacy_begin(d3d3_device_from_iface(iface), primitive_type,
            fvf, vertices, vertex_count);
}

static HRESULT WINAPI d3d3_device_Vertex(IDirect3DDevice3 *iface,
        void *vertex)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    D3D7FVFLayout layout;
    D9WGVertexElement elements[D3D7_MAX_VERTEX_ELEMENTS];
    UINT element_count = 0;
    if (!device->legacy_in_begin) return DDWG_TRACE_ERROR(D3DERR_NOTINBEGIN);
    if (!vertex || !d3d7_decode_fvf(device->legacy_fvf, &layout, elements,
            &element_count)) return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXTYPE);
    if (!d3d_legacy_grow((void **)&device->legacy_vertices,
            &device->legacy_vertex_capacity, device->legacy_vertex_count + 1u,
            layout.stride)) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    CopyMemory(device->legacy_vertices +
            device->legacy_vertex_count * layout.stride, vertex,
            layout.stride);
    ++device->legacy_vertex_count;
    return D3D_OK;
}

static HRESULT WINAPI d3d3_device_Index(IDirect3DDevice3 *iface, WORD index)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    if (!device->legacy_in_begin) return DDWG_TRACE_ERROR(D3DERR_NOTINBEGIN);
    if (!d3d_legacy_grow((void **)&device->legacy_indices,
            &device->legacy_index_capacity, device->legacy_index_count + 1u,
            sizeof(WORD))) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    device->legacy_indices[device->legacy_index_count++] = index;
    return D3D_OK;
}

static HRESULT WINAPI d3d3_device_End(IDirect3DDevice3 *iface, DWORD flags)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    HRESULT result;
    UINT primitive_count;
    (void)flags;
    if (!device->legacy_in_begin) return DDWG_TRACE_ERROR(D3DERR_NOTINBEGIN);
    device->legacy_in_begin = FALSE;
    if (!device->legacy_vertex_count) {
        device->legacy_index_count = 0;
        return D3D_OK;
    }
    if (device->legacy_index_count) {
        result = d3d7_device_DrawIndexedPrimitive((IDirect3DDevice7 *)device,
                device->legacy_primitive_type, device->legacy_fvf,
                device->legacy_vertices, device->legacy_vertex_count,
                device->legacy_indices, device->legacy_index_count, 0);
        d3d7_primitive_counts(device->legacy_primitive_type,
                device->legacy_index_count, &primitive_count);
    } else {
        result = d3d7_device_DrawPrimitive((IDirect3DDevice7 *)device,
                device->legacy_primitive_type, device->legacy_fvf,
                device->legacy_vertices, device->legacy_vertex_count, 0);
        d3d7_primitive_counts(device->legacy_primitive_type,
                device->legacy_vertex_count, &primitive_count);
    }
    if (SUCCEEDED(result)) {
        device->legacy_stats.dwVerticesProcessed += device->legacy_vertex_count;
        if (device->legacy_primitive_type == D3DPT_POINTLIST)
            device->legacy_stats.dwPointsDrawn += primitive_count;
        else if (device->legacy_primitive_type == D3DPT_LINELIST ||
                device->legacy_primitive_type == D3DPT_LINESTRIP)
            device->legacy_stats.dwLinesDrawn += primitive_count;
        else device->legacy_stats.dwTrianglesDrawn += primitive_count;
    }
    device->legacy_vertex_count = device->legacy_index_count = 0;
    return DDWG_TRACE_ERROR(result);
}

static DDSurface *d3d_legacy_find_texture(DDrawObject *owner,
        D3DTEXTUREHANDLE handle)
{
    DDSurface *surface;
    for (surface = owner->surfaces; surface; surface = surface->next) {
        if (surface->texture_handle == handle) return surface;
    }
    return NULL;
}

static HRESULT WINAPI d3d3_device_GetRenderState(IDirect3DDevice3 *iface,
        D3DRENDERSTATETYPE state, DWORD *value)
{
    return d3d7_device_GetRenderState((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), state, value);
}

static HRESULT WINAPI d3d3_device_SetRenderState(IDirect3DDevice3 *iface,
        D3DRENDERSTATETYPE state, DWORD value)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    if (state == D3DRENDERSTATE_TEXTUREHANDLE) {
        DDSurface *surface = value ? d3d_legacy_find_texture(device->owner,
                value) : NULL;
        HRESULT result;
        if (value && !surface) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        result = d3d7_device_SetTexture((IDirect3DDevice7 *)device, 0,
                surface ? (IDirectDrawSurface7 *)&surface->vtbl7 : NULL);
        if (SUCCEEDED(result))
            device->render_states[D3DRENDERSTATE_TEXTUREHANDLE] = value;
        return DDWG_TRACE_ERROR(result);
    }
    return d3d7_device_SetRenderState((IDirect3DDevice7 *)device, state,
            value);
}

static HRESULT WINAPI d3d3_device_GetLightState(IDirect3DDevice3 *iface,
        D3DLIGHTSTATETYPE state, DWORD *value)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    if (!value) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    switch (state) {
    case D3DLIGHTSTATE_MATERIAL: *value = device->legacy_material_handle; break;
    case D3DLIGHTSTATE_AMBIENT:
        *value = device->render_states[D3DRENDERSTATE_AMBIENT]; break;
    case D3DLIGHTSTATE_COLORMODEL: *value = D3DCOLOR_RGB; break;
    case D3DLIGHTSTATE_FOGMODE:
        *value = device->render_states[D3DRENDERSTATE_FOGVERTEXMODE]; break;
    case D3DLIGHTSTATE_FOGSTART:
        *value = device->render_states[D3DRENDERSTATE_FOGSTART]; break;
    case D3DLIGHTSTATE_FOGEND:
        *value = device->render_states[D3DRENDERSTATE_FOGEND]; break;
    case D3DLIGHTSTATE_FOGDENSITY:
        *value = device->render_states[D3DRENDERSTATE_FOGDENSITY]; break;
    case D3DLIGHTSTATE_COLORVERTEX:
        *value = device->render_states[D3DRENDERSTATE_COLORVERTEX]; break;
    default: return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    }
    return D3D_OK;
}

static HRESULT WINAPI d3d3_device_SetLightState(IDirect3DDevice3 *iface,
        D3DLIGHTSTATETYPE state, DWORD value)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    switch (state) {
    case D3DLIGHTSTATE_MATERIAL: {
        D3DLegacyMaterial *material = d3d_legacy_find_material(device->owner,
                value);
        D3DMATERIAL7 material7;
        if (value && !material) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        if (material)
            d3d_material_AddRef((IDirect3DMaterial3 *)&material->vtbl3);
        if (device->legacy_material)
            d3d_material_Release((IDirect3DMaterial3 *)&
                    device->legacy_material->vtbl3);
        device->legacy_material = material;
        device->legacy_material_handle = value;
        if (!material) return D3D_OK;
        d3d_legacy_material7(&material->material, &material7);
        return d3d7_device_SetMaterial((IDirect3DDevice7 *)device, &material7);
    }
    case D3DLIGHTSTATE_AMBIENT:
        return d3d7_device_SetRenderState((IDirect3DDevice7 *)device,
                D3DRENDERSTATE_AMBIENT, value);
    case D3DLIGHTSTATE_COLORMODEL:
        return DDWG_TRACE_ERROR(value == D3DCOLOR_RGB
                ? D3D_OK : DDERR_UNSUPPORTED);
    case D3DLIGHTSTATE_FOGMODE:
        return d3d7_device_SetRenderState((IDirect3DDevice7 *)device,
                D3DRENDERSTATE_FOGVERTEXMODE, value);
    case D3DLIGHTSTATE_FOGSTART:
        return d3d7_device_SetRenderState((IDirect3DDevice7 *)device,
                D3DRENDERSTATE_FOGSTART, value);
    case D3DLIGHTSTATE_FOGEND:
        return d3d7_device_SetRenderState((IDirect3DDevice7 *)device,
                D3DRENDERSTATE_FOGEND, value);
    case D3DLIGHTSTATE_FOGDENSITY:
        return d3d7_device_SetRenderState((IDirect3DDevice7 *)device,
                D3DRENDERSTATE_FOGDENSITY, value);
    case D3DLIGHTSTATE_COLORVERTEX:
        return d3d7_device_SetRenderState((IDirect3DDevice7 *)device,
                D3DRENDERSTATE_COLORVERTEX, value);
    default: return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    }
}

static HRESULT WINAPI d3d3_device_SetTransform(IDirect3DDevice3 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    return d3d7_device_SetTransform((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), state, matrix);
}

static HRESULT WINAPI d3d3_device_GetTransform(IDirect3DDevice3 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    return d3d7_device_GetTransform((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), state, matrix);
}

static HRESULT WINAPI d3d3_device_MultiplyTransform(IDirect3DDevice3 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    return d3d7_device_MultiplyTransform((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), state, matrix);
}

static HRESULT WINAPI d3d3_device_DrawPrimitive(IDirect3DDevice3 *iface,
        D3DPRIMITIVETYPE primitive_type, DWORD fvf, void *vertices,
        DWORD vertex_count, DWORD flags)
{
    return d3d7_device_DrawPrimitive((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), primitive_type, fvf, vertices,
            vertex_count, flags);
}

static HRESULT WINAPI d3d3_device_DrawIndexedPrimitive(
        IDirect3DDevice3 *iface, D3DPRIMITIVETYPE primitive_type, DWORD fvf,
        void *vertices, DWORD vertex_count, WORD *indices, DWORD index_count,
        DWORD flags)
{
    return d3d7_device_DrawIndexedPrimitive((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), primitive_type, fvf, vertices,
            vertex_count, indices, index_count, flags);
}

static HRESULT WINAPI d3d3_device_SetClipStatus(IDirect3DDevice3 *iface,
        D3DCLIPSTATUS *status)
{
    return d3d7_device_SetClipStatus((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), status);
}

static HRESULT WINAPI d3d3_device_GetClipStatus(IDirect3DDevice3 *iface,
        D3DCLIPSTATUS *status)
{
    return d3d7_device_GetClipStatus((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), status);
}

static HRESULT WINAPI d3d3_device_DrawPrimitiveStrided(
        IDirect3DDevice3 *iface, D3DPRIMITIVETYPE primitive_type, DWORD fvf,
        D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vertex_count, DWORD flags)
{
    return d3d7_device_DrawPrimitiveStrided((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), primitive_type, fvf, data,
            vertex_count, flags);
}

static HRESULT WINAPI d3d3_device_DrawIndexedPrimitiveStrided(
        IDirect3DDevice3 *iface, D3DPRIMITIVETYPE primitive_type, DWORD fvf,
        D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vertex_count, WORD *indices,
        DWORD index_count, DWORD flags)
{
    return d3d7_device_DrawIndexedPrimitiveStrided((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), primitive_type, fvf, data,
            vertex_count, indices, index_count, flags);
}

static HRESULT WINAPI d3d3_device_DrawPrimitiveVB(IDirect3DDevice3 *iface,
        D3DPRIMITIVETYPE primitive_type, IDirect3DVertexBuffer *buffer_iface,
        DWORD start_vertex, DWORD vertex_count, DWORD flags)
{
    D3D7VertexBuffer *buffer;
    if (!buffer_iface || buffer_iface->lpVtbl != &g_d3d_vb_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    buffer = d3d_vb_from_iface(buffer_iface);
    return d3d7_device_DrawPrimitiveVB((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), primitive_type,
            (IDirect3DVertexBuffer7 *)buffer, start_vertex, vertex_count,
            flags);
}

static HRESULT WINAPI d3d3_device_DrawIndexedPrimitiveVB(
        IDirect3DDevice3 *iface, D3DPRIMITIVETYPE primitive_type,
        IDirect3DVertexBuffer *buffer_iface, WORD *indices, DWORD index_count,
        DWORD flags)
{
    D3D7VertexBuffer *buffer;
    if (!buffer_iface || buffer_iface->lpVtbl != &g_d3d_vb_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    buffer = d3d_vb_from_iface(buffer_iface);
    return d3d7_device_DrawIndexedPrimitiveVB((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), primitive_type,
            (IDirect3DVertexBuffer7 *)buffer, 0, buffer->desc.dwNumVertices,
            indices, index_count, flags);
}

static HRESULT WINAPI d3d3_device_ComputeSphereVisibility(
        IDirect3DDevice3 *iface, D3DVECTOR *centers, D3DVALUE *radii,
        DWORD sphere_count, DWORD flags, DWORD *results)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    D3DMATRIX world_view, matrix;
    float planes[6][4];
    DWORD index, plane;
    (void)flags;
    if (sphere_count && (!centers || !radii || !results))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    d3d7_multiply_matrix(&device->transforms[D3DTRANSFORMSTATE_WORLD],
            &device->transforms[D3DTRANSFORMSTATE_VIEW], &world_view);
    d3d7_multiply_matrix(&world_view,
            &device->transforms[D3DTRANSFORMSTATE_PROJECTION], &matrix);
#define D3D3_SET_PLANE(dst, sign, column) do { \
    planes[(dst)][0] = matrix._14 sign matrix.column##1; \
    planes[(dst)][1] = matrix._24 sign matrix.column##2; \
    planes[(dst)][2] = matrix._34 sign matrix.column##3; \
    planes[(dst)][3] = matrix._44 sign matrix.column##4; \
} while (0)
    D3D3_SET_PLANE(0, +, _1);
    D3D3_SET_PLANE(1, -, _1);
    D3D3_SET_PLANE(2, -, _2);
    D3D3_SET_PLANE(3, +, _2);
#undef D3D3_SET_PLANE
    planes[4][0] = matrix._13;
    planes[4][1] = matrix._23;
    planes[4][2] = matrix._33;
    planes[4][3] = matrix._43;
    planes[5][0] = matrix._14 - matrix._13;
    planes[5][1] = matrix._24 - matrix._23;
    planes[5][2] = matrix._34 - matrix._33;
    planes[5][3] = matrix._44 - matrix._43;
    for (index = 0; index < sphere_count; ++index) {
        DWORD visibility = D3DVIS_INSIDE_FRUSTUM;
        BOOL intersects = FALSE, outside = FALSE;
        for (plane = 0; plane < 6u; ++plane) {
            float length = d3d7_sqrt(planes[plane][0] * planes[plane][0] +
                    planes[plane][1] * planes[plane][1] +
                    planes[plane][2] * planes[plane][2]);
            float distance;
            if (!(length > 0.0f)) continue;
            distance = (planes[plane][0] * centers[index].x +
                    planes[plane][1] * centers[index].y +
                    planes[plane][2] * centers[index].z + planes[plane][3]) /
                    length;
            if (distance < -radii[index]) {
                visibility |= D3DVIS_OUTSIDE_LEFT << (plane * 2u);
                outside = TRUE;
            } else if (distance <= radii[index]) {
                visibility |= D3DVIS_INTERSECT_LEFT << (plane * 2u);
                intersects = TRUE;
            }
        }
        visibility |= outside ? D3DVIS_OUTSIDE_FRUSTUM :
                (intersects ? D3DVIS_INTERSECT_FRUSTUM :
                D3DVIS_INSIDE_FRUSTUM);
        results[index] = visibility;
    }
    return D3D_OK;
}

static HRESULT WINAPI d3d3_device_GetTexture(IDirect3DDevice3 *iface,
        DWORD stage, IDirect3DTexture2 **out)
{
    D3D7Device *device = d3d3_device_from_iface(iface);
    DDSurface *surface;
    if (!out || stage >= D3D7_MAX_TEXTURE_STAGES)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    surface = device->textures[stage];
    *out = surface ? (IDirect3DTexture2 *)&surface->texture2_vtbl : NULL;
    if (surface) surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
    return D3D_OK;
}

static HRESULT WINAPI d3d3_device_SetTexture(IDirect3DDevice3 *iface,
        DWORD stage, IDirect3DTexture2 *texture)
{
    DDSurface *surface = NULL;
    if (texture) {
        if (texture->lpVtbl != &g_d3d_texture2_vtbl)
            return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        surface = d3d_texture2_from_iface(texture);
    }
    return d3d7_device_SetTexture((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), stage, surface ?
            (IDirectDrawSurface7 *)&surface->vtbl7 : NULL);
}

static HRESULT WINAPI d3d3_device_GetTextureStageState(
        IDirect3DDevice3 *iface, DWORD stage, D3DTEXTURESTAGESTATETYPE state,
        DWORD *value)
{
    return d3d7_device_GetTextureStageState((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), stage, state, value);
}

static HRESULT WINAPI d3d3_device_SetTextureStageState(
        IDirect3DDevice3 *iface, DWORD stage, D3DTEXTURESTAGESTATETYPE state,
        DWORD value)
{
    return d3d7_device_SetTextureStageState((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), stage, state, value);
}

static HRESULT WINAPI d3d3_device_ValidateDevice(IDirect3DDevice3 *iface,
        DWORD *passes)
{
    return d3d7_device_ValidateDevice((IDirect3DDevice7 *)
            d3d3_device_from_iface(iface), passes);
}

/* ------------------------------------------------------------------ *
 * IDirect3DDevice2 view
 * ------------------------------------------------------------------ */

static HRESULT WINAPI d3d2_device_QueryInterface(IDirect3DDevice2 *iface,
        REFIID iid, void **out)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    return d3d3_device_QueryInterface((IDirect3DDevice3 *)&
            device->device3_vtbl, iid, out);
}

static ULONG WINAPI d3d2_device_AddRef(IDirect3DDevice2 *iface)
{
    return d3d7_device_AddRef((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface));
}

static ULONG WINAPI d3d2_device_Release(IDirect3DDevice2 *iface)
{
    return d3d7_device_Release((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface));
}

static HRESULT WINAPI d3d2_device_GetCaps(IDirect3DDevice2 *iface,
        D3DDEVICEDESC *hal, D3DDEVICEDESC *hel)
{
    (void)iface;
    return d3d_legacy_device_get_caps(hal, hel);
}

static HRESULT WINAPI d3d2_device_SwapTextureHandles(IDirect3DDevice2 *iface,
        IDirect3DTexture2 *first_iface, IDirect3DTexture2 *second_iface)
{
    DDSurface *first, *second;
    D3DTEXTUREHANDLE handle;
    D3D7Device *device = d3d2_device_from_iface(iface);
    if (!first_iface || !second_iface ||
            first_iface->lpVtbl != &g_d3d_texture2_vtbl ||
            second_iface->lpVtbl != &g_d3d_texture2_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    first = d3d_texture2_from_iface(first_iface);
    second = d3d_texture2_from_iface(second_iface);
    if (first->owner != device->owner || second->owner != device->owner)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    handle = first->texture_handle;
    first->texture_handle = second->texture_handle;
    second->texture_handle = handle;
    if (device->render_states[D3DRENDERSTATE_TEXTUREHANDLE])
        return d3d3_device_SetRenderState((IDirect3DDevice3 *)&
                device->device3_vtbl, D3DRENDERSTATE_TEXTUREHANDLE,
                device->render_states[D3DRENDERSTATE_TEXTUREHANDLE]);
    return D3D_OK;
}

static HRESULT WINAPI d3d2_device_GetStats(IDirect3DDevice2 *iface,
        D3DSTATS *stats)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    return d3d3_device_GetStats((IDirect3DDevice3 *)&device->device3_vtbl,
            stats);
}

static HRESULT WINAPI d3d2_device_AddViewport(IDirect3DDevice2 *iface,
        IDirect3DViewport2 *viewport)
{
    return d3d_legacy_add_viewport(d3d2_device_from_iface(iface),
            viewport ? d3d_viewport_from_any(viewport) : NULL);
}

static HRESULT WINAPI d3d2_device_DeleteViewport(IDirect3DDevice2 *iface,
        IDirect3DViewport2 *viewport)
{
    return d3d_legacy_delete_viewport(d3d2_device_from_iface(iface),
            viewport ? d3d_viewport_from_any(viewport) : NULL);
}

static HRESULT WINAPI d3d2_device_NextViewport(IDirect3DDevice2 *iface,
        IDirect3DViewport2 *reference, IDirect3DViewport2 **out, DWORD flags)
{
    D3DLegacyViewport *viewport;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    viewport = d3d_legacy_next_viewport(d3d2_device_from_iface(iface),
            reference ? d3d_viewport_from_any(reference) : NULL, flags);
    if (!viewport) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
    *out = (IDirect3DViewport2 *)&viewport->vtbl2;
    d3d_viewport_AddRef((IDirect3DViewport3 *)&viewport->vtbl3);
    return D3D_OK;
}

static HRESULT WINAPI d3d2_device_EnumTextureFormats(
        IDirect3DDevice2 *iface, LPD3DENUMTEXTUREFORMATSCALLBACK callback,
        void *context)
{
    D3DLegacyTextureEnumContext adapter;
    (void)iface;
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    adapter.callback = callback;
    adapter.context = context;
    return d3d7_enum_texture_formats(d3d_legacy_texture_enum_callback,
            &adapter, FALSE);
}

static HRESULT WINAPI d3d2_device_BeginScene(IDirect3DDevice2 *iface)
{
    return d3d7_device_BeginScene((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface));
}

static HRESULT WINAPI d3d2_device_EndScene(IDirect3DDevice2 *iface)
{
    return d3d7_device_EndScene((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface));
}

static HRESULT WINAPI d3d2_device_GetDirect3D(IDirect3DDevice2 *iface,
        IDirect3D2 **out)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = (IDirect3D2 *)&device->owner->d3d2_vtbl;
    d3d2_AddRef(*out);
    return D3D_OK;
}

static HRESULT WINAPI d3d2_device_SetCurrentViewport(IDirect3DDevice2 *iface,
        IDirect3DViewport2 *viewport)
{
    return d3d_legacy_set_current_viewport(d3d2_device_from_iface(iface),
            viewport ? d3d_viewport_from_any(viewport) : NULL);
}

static HRESULT WINAPI d3d2_device_GetCurrentViewport(IDirect3DDevice2 *iface,
        IDirect3DViewport2 **out)
{
    D3DLegacyViewport *viewport =
            d3d2_device_from_iface(iface)->legacy_current_viewport;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = viewport ? (IDirect3DViewport2 *)&viewport->vtbl2 : NULL;
    if (!viewport) return DDWG_TRACE_ERROR(D3DERR_NOCURRENTVIEWPORT);
    d3d_viewport_AddRef((IDirect3DViewport3 *)&viewport->vtbl3);
    return D3D_OK;
}

static HRESULT WINAPI d3d2_device_SetRenderTarget(IDirect3DDevice2 *iface,
        IDirectDrawSurface *surface, DWORD flags)
{
    DDSurface *target = surface ? surface_from_any(surface, NULL) : NULL;
    return d3d7_device_SetRenderTarget((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface), target ?
            (IDirectDrawSurface7 *)&target->vtbl7 : NULL, flags);
}

static HRESULT WINAPI d3d2_device_GetRenderTarget(IDirect3DDevice2 *iface,
        IDirectDrawSurface **out)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = device->render_target ? (IDirectDrawSurface *)&
            device->render_target->vtbl1 : NULL;
    if (device->render_target)
        surface_AddRef((IDirectDrawSurface7 *)&device->render_target->vtbl7);
    return DDWG_TRACE_ERROR(device->render_target ? D3D_OK : DDERR_NOTFOUND);
}

static HRESULT WINAPI d3d2_device_Begin(IDirect3DDevice2 *iface,
        D3DPRIMITIVETYPE primitive_type, D3DVERTEXTYPE vertex_type,
        DWORD flags)
{
    DWORD fvf = d3d_legacy_vertex_type_fvf(vertex_type);
    if (!fvf) return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXTYPE);
    (void)flags;
    return d3d_legacy_begin(d3d2_device_from_iface(iface), primitive_type,
            fvf, NULL, 0);
}

static HRESULT WINAPI d3d2_device_BeginIndexed(IDirect3DDevice2 *iface,
        D3DPRIMITIVETYPE primitive_type, D3DVERTEXTYPE vertex_type,
        void *vertices, DWORD vertex_count, DWORD flags)
{
    DWORD fvf = d3d_legacy_vertex_type_fvf(vertex_type);
    if (!fvf) return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXTYPE);
    (void)flags;
    return d3d_legacy_begin(d3d2_device_from_iface(iface), primitive_type,
            fvf, vertices, vertex_count);
}

static HRESULT WINAPI d3d2_device_Vertex(IDirect3DDevice2 *iface,
        void *vertex)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    return d3d3_device_Vertex((IDirect3DDevice3 *)&device->device3_vtbl,
            vertex);
}

static HRESULT WINAPI d3d2_device_Index(IDirect3DDevice2 *iface, WORD index)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    return d3d3_device_Index((IDirect3DDevice3 *)&device->device3_vtbl,
            index);
}

static HRESULT WINAPI d3d2_device_End(IDirect3DDevice2 *iface, DWORD flags)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    return d3d3_device_End((IDirect3DDevice3 *)&device->device3_vtbl, flags);
}

static HRESULT WINAPI d3d2_device_GetRenderState(IDirect3DDevice2 *iface,
        D3DRENDERSTATETYPE state, DWORD *value)
{
    return d3d7_device_GetRenderState((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface), state, value);
}

static HRESULT WINAPI d3d2_device_SetRenderState(IDirect3DDevice2 *iface,
        D3DRENDERSTATETYPE state, DWORD value)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    return d3d3_device_SetRenderState((IDirect3DDevice3 *)&
            device->device3_vtbl, state, value);
}

static HRESULT WINAPI d3d2_device_GetLightState(IDirect3DDevice2 *iface,
        D3DLIGHTSTATETYPE state, DWORD *value)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    return d3d3_device_GetLightState((IDirect3DDevice3 *)&
            device->device3_vtbl, state, value);
}

static HRESULT WINAPI d3d2_device_SetLightState(IDirect3DDevice2 *iface,
        D3DLIGHTSTATETYPE state, DWORD value)
{
    D3D7Device *device = d3d2_device_from_iface(iface);
    return d3d3_device_SetLightState((IDirect3DDevice3 *)&
            device->device3_vtbl, state, value);
}

static HRESULT WINAPI d3d2_device_SetTransform(IDirect3DDevice2 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    return d3d7_device_SetTransform((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface), state, matrix);
}

static HRESULT WINAPI d3d2_device_GetTransform(IDirect3DDevice2 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    return d3d7_device_GetTransform((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface), state, matrix);
}

static HRESULT WINAPI d3d2_device_MultiplyTransform(IDirect3DDevice2 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    return d3d7_device_MultiplyTransform((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface), state, matrix);
}

static HRESULT WINAPI d3d2_device_DrawPrimitive(IDirect3DDevice2 *iface,
        D3DPRIMITIVETYPE primitive_type, D3DVERTEXTYPE vertex_type,
        void *vertices, DWORD vertex_count, DWORD flags)
{
    DWORD fvf = d3d_legacy_vertex_type_fvf(vertex_type);
    if (!fvf) return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXTYPE);
    return d3d7_device_DrawPrimitive((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface), primitive_type, fvf, vertices,
            vertex_count, flags);
}

static HRESULT WINAPI d3d2_device_DrawIndexedPrimitive(
        IDirect3DDevice2 *iface, D3DPRIMITIVETYPE primitive_type,
        D3DVERTEXTYPE vertex_type, void *vertices, DWORD vertex_count,
        WORD *indices, DWORD index_count, DWORD flags)
{
    DWORD fvf = d3d_legacy_vertex_type_fvf(vertex_type);
    if (!fvf) return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXTYPE);
    return d3d7_device_DrawIndexedPrimitive((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface), primitive_type, fvf, vertices,
            vertex_count, indices, index_count, flags);
}

static HRESULT WINAPI d3d2_device_SetClipStatus(IDirect3DDevice2 *iface,
        D3DCLIPSTATUS *status)
{
    return d3d7_device_SetClipStatus((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface), status);
}

static HRESULT WINAPI d3d2_device_GetClipStatus(IDirect3DDevice2 *iface,
        D3DCLIPSTATUS *status)
{
    return d3d7_device_GetClipStatus((IDirect3DDevice7 *)
            d3d2_device_from_iface(iface), status);
}

/* ------------------------------------------------------------------ *
 * IDirect3DVertexBuffer (DirectX 5/6)
 * ------------------------------------------------------------------ */

static HRESULT WINAPI d3d_vb_QueryInterface(IDirect3DVertexBuffer *iface,
        REFIID iid, void **out)
{
    return d3d7_vb_QueryInterface((IDirect3DVertexBuffer7 *)
            d3d_vb_from_iface(iface), iid, out);
}

static ULONG WINAPI d3d_vb_AddRef(IDirect3DVertexBuffer *iface)
{
    return d3d7_vb_AddRef((IDirect3DVertexBuffer7 *)
            d3d_vb_from_iface(iface));
}

static ULONG WINAPI d3d_vb_Release(IDirect3DVertexBuffer *iface)
{
    return d3d7_vb_Release((IDirect3DVertexBuffer7 *)
            d3d_vb_from_iface(iface));
}

static HRESULT WINAPI d3d_vb_Lock(IDirect3DVertexBuffer *iface, DWORD flags,
        void **data, DWORD *data_size)
{
    return d3d7_vb_Lock((IDirect3DVertexBuffer7 *)d3d_vb_from_iface(iface),
            flags, data, data_size);
}

static HRESULT WINAPI d3d_vb_Unlock(IDirect3DVertexBuffer *iface)
{
    return d3d7_vb_Unlock((IDirect3DVertexBuffer7 *)d3d_vb_from_iface(iface));
}

static HRESULT WINAPI d3d_vb_ProcessVertices(IDirect3DVertexBuffer *iface,
        DWORD vertex_op, DWORD destination_index, DWORD count,
        IDirect3DVertexBuffer *source_iface, DWORD source_index,
        IDirect3DDevice3 *device, DWORD flags)
{
    D3D7VertexBuffer *destination = d3d_vb_from_iface(iface);
    D3D7VertexBuffer *source;
    if (!source_iface || source_iface->lpVtbl != &g_d3d_vb_vtbl || !device)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    source = d3d_vb_from_iface(source_iface);
    return d3d7_vb_ProcessVertices((IDirect3DVertexBuffer7 *)destination,
            vertex_op, destination_index, count,
            (IDirect3DVertexBuffer7 *)source, source_index,
            (IDirect3DDevice7 *)d3d3_device_from_iface(device), flags);
}

static HRESULT WINAPI d3d_vb_GetVertexBufferDesc(
        IDirect3DVertexBuffer *iface, D3DVERTEXBUFFERDESC *description)
{
    return d3d7_vb_GetVertexBufferDesc((IDirect3DVertexBuffer7 *)
            d3d_vb_from_iface(iface), description);
}

static HRESULT WINAPI d3d_vb_Optimize(IDirect3DVertexBuffer *iface,
        IDirect3DDevice3 *device, DWORD flags)
{
    return d3d7_vb_Optimize((IDirect3DVertexBuffer7 *)d3d_vb_from_iface(iface),
            device ? (IDirect3DDevice7 *)d3d3_device_from_iface(device) : NULL,
            flags);
}

/* ------------------------------------------------------------------ *
 * Direct3D 1 device, matrices and execute buffers
 * ------------------------------------------------------------------ */

static HRESULT d3d_legacy_surface_create_device(DDSurface *surface,
        REFIID iid, void **out)
{
    const GUID *device_class;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (surface->owner->d3d7_device) {
        D3D7Device *device = surface->owner->d3d7_device;
        if (device->render_target != surface) return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
        d3d7_device_AddRef((IDirect3DDevice7 *)device);
        *out = &device->device1_vtbl;
        return D3D_OK;
    }
    if (guid_equal(iid, &IID_IDirect3DHALDevice))
        device_class = &IID_IDirect3DHALDevice;
    else
        device_class = &IID_IDirect3DRGBDevice;
    return d3d_legacy_create_device(surface->owner, device_class, surface, 1u,
            out);
}

static HRESULT WINAPI d3d1_device_QueryInterface(IDirect3DDevice *iface,
        REFIID iid, void **out)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    return d3d3_device_QueryInterface((IDirect3DDevice3 *)&
            device->device3_vtbl, iid, out);
}

static ULONG WINAPI d3d1_device_AddRef(IDirect3DDevice *iface)
{
    return d3d7_device_AddRef((IDirect3DDevice7 *)
            d3d1_device_from_iface(iface));
}

static ULONG WINAPI d3d1_device_Release(IDirect3DDevice *iface)
{
    return d3d7_device_Release((IDirect3DDevice7 *)
            d3d1_device_from_iface(iface));
}

static HRESULT WINAPI d3d1_device_Initialize(IDirect3DDevice *iface,
        IDirect3D *d3d, GUID *guid, D3DDEVICEDESC *description)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    (void)description;
    if (!d3d || d3d1_object_from_iface(d3d) != device->owner || !guid)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    return DDWG_TRACE_ERROR(DDERR_ALREADYINITIALIZED);
}

static HRESULT WINAPI d3d1_device_GetCaps(IDirect3DDevice *iface,
        D3DDEVICEDESC *hal, D3DDEVICEDESC *hel)
{
    (void)iface;
    return d3d_legacy_device_get_caps(hal, hel);
}

static HRESULT WINAPI d3d1_device_SwapTextureHandles(IDirect3DDevice *iface,
        IDirect3DTexture *first_iface, IDirect3DTexture *second_iface)
{
    DDSurface *first, *second;
    D3DTEXTUREHANDLE handle;
    D3D7Device *device = d3d1_device_from_iface(iface);
    if (!first_iface || !second_iface ||
            first_iface->lpVtbl != &g_d3d_texture1_vtbl ||
            second_iface->lpVtbl != &g_d3d_texture1_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    first = d3d_texture1_from_iface(first_iface);
    second = d3d_texture1_from_iface(second_iface);
    if (first->owner != device->owner || second->owner != device->owner)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    handle = first->texture_handle;
    first->texture_handle = second->texture_handle;
    second->texture_handle = handle;
    return D3D_OK;
}

static D3DLegacyExecuteBuffer *d3d_execute_buffer_from_iface(
        IDirect3DExecuteBuffer *iface)
{
    return (D3DLegacyExecuteBuffer *)iface;
}

static HRESULT WINAPI d3d_execute_buffer_QueryInterface(
        IDirect3DExecuteBuffer *iface, REFIID iid, void **out)
{
    if (!out) return DDWG_TRACE_ERROR(E_POINTER);
    *out = NULL;
    if (!iid_is_unknown(iid) && !guid_equal(iid, &IID_IDirect3DExecuteBuffer))
        return DDWG_TRACE_ERROR(E_NOINTERFACE);
    *out = iface;
    InterlockedIncrement(&d3d_execute_buffer_from_iface(iface)->ref);
    return S_OK;
}

static ULONG WINAPI d3d_execute_buffer_AddRef(IDirect3DExecuteBuffer *iface)
{
    return (ULONG)InterlockedIncrement(&
            d3d_execute_buffer_from_iface(iface)->ref);
}

static ULONG WINAPI d3d_execute_buffer_Release(IDirect3DExecuteBuffer *iface)
{
    D3DLegacyExecuteBuffer *buffer = d3d_execute_buffer_from_iface(iface);
    LONG remaining = InterlockedDecrement(&buffer->ref);
    if (!remaining) {
        D3D7Device *device = buffer->device;
        heap_free(buffer->processed);
        heap_free(buffer->memory);
        heap_free(buffer);
        d3d7_device_Release((IDirect3DDevice7 *)device);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

static HRESULT WINAPI d3d_execute_buffer_Initialize(
        IDirect3DExecuteBuffer *iface, IDirect3DDevice *device,
        D3DEXECUTEBUFFERDESC *description)
{
    D3DLegacyExecuteBuffer *buffer = d3d_execute_buffer_from_iface(iface);
    (void)description;
    return device && d3d1_device_from_iface(device) == buffer->device ?
            DDERR_ALREADYINITIALIZED : DDERR_INVALIDPARAMS;
}

static HRESULT WINAPI d3d_execute_buffer_Lock(IDirect3DExecuteBuffer *iface,
        D3DEXECUTEBUFFERDESC *description)
{
    D3DLegacyExecuteBuffer *buffer = d3d_execute_buffer_from_iface(iface);
    DWORD requested;
    if (!description || description->dwSize < sizeof(DWORD))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (buffer->locked) return DDWG_TRACE_ERROR(D3DERR_EXECUTE_LOCKED);
    requested = description->dwSize;
    ZeroMemory(description, requested < sizeof(*description) ? requested :
            sizeof(*description));
    description->dwSize = requested;
    if (requested >= offsetof(D3DEXECUTEBUFFERDESC, dwFlags) + sizeof(DWORD))
        description->dwFlags = D3DDEB_BUFSIZE | D3DDEB_CAPS | D3DDEB_LPDATA;
    if (requested >= offsetof(D3DEXECUTEBUFFERDESC, dwCaps) + sizeof(DWORD))
        description->dwCaps = buffer->caps;
    if (requested >= offsetof(D3DEXECUTEBUFFERDESC, dwBufferSize) +
            sizeof(DWORD)) description->dwBufferSize = buffer->byte_count;
    if (requested >= offsetof(D3DEXECUTEBUFFERDESC, lpData) + sizeof(void *))
        description->lpData = buffer->memory;
    buffer->locked = TRUE;
    return D3D_OK;
}

static HRESULT WINAPI d3d_execute_buffer_Unlock(IDirect3DExecuteBuffer *iface)
{
    D3DLegacyExecuteBuffer *buffer = d3d_execute_buffer_from_iface(iface);
    if (!buffer->locked) return DDWG_TRACE_ERROR(D3DERR_EXECUTE_NOT_LOCKED);
    buffer->locked = FALSE;
    return D3D_OK;
}

static BOOL d3d_execute_data_valid(D3DLegacyExecuteBuffer *buffer,
        const D3DEXECUTEDATA *data)
{
    uint64_t vertices_end, instructions_end;
    if (!data || data->dwSize != sizeof(*data)) return FALSE;
    vertices_end = (uint64_t)data->dwVertexOffset +
            (uint64_t)data->dwVertexCount * sizeof(D3DVERTEX);
    instructions_end = (uint64_t)data->dwInstructionOffset +
            data->dwInstructionLength;
    return vertices_end <= buffer->byte_count &&
            instructions_end <= buffer->byte_count &&
            data->dwHVertexOffset <= buffer->byte_count;
}

static HRESULT WINAPI d3d_execute_buffer_SetExecuteData(
        IDirect3DExecuteBuffer *iface, D3DEXECUTEDATA *data)
{
    D3DLegacyExecuteBuffer *buffer = d3d_execute_buffer_from_iface(iface);
    D3DTLVERTEX *processed = NULL;
    if (buffer->locked) return DDWG_TRACE_ERROR(D3DERR_EXECUTE_LOCKED);
    if (!d3d_execute_data_valid(buffer, data)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (data->dwVertexCount) {
        processed = (D3DTLVERTEX *)heap_alloc_zero((SIZE_T)data->dwVertexCount *
                sizeof(*processed));
        if (!processed) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    }
    heap_free(buffer->processed);
    buffer->processed = processed;
    buffer->processed_count = data->dwVertexCount;
    buffer->data = *data;
    return D3D_OK;
}

static HRESULT WINAPI d3d_execute_buffer_GetExecuteData(
        IDirect3DExecuteBuffer *iface, D3DEXECUTEDATA *data)
{
    if (!data) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *data = d3d_execute_buffer_from_iface(iface)->data;
    return D3D_OK;
}

static UINT d3d_execute_expected_size(BYTE opcode)
{
    switch (opcode) {
    case D3DOP_POINT: return sizeof(D3DPOINT);
    case D3DOP_LINE: return sizeof(D3DLINE);
    case D3DOP_TRIANGLE: return sizeof(D3DTRIANGLE);
    case D3DOP_MATRIXLOAD: return sizeof(D3DMATRIXLOAD);
    case D3DOP_MATRIXMULTIPLY: return sizeof(D3DMATRIXMULTIPLY);
    case D3DOP_STATETRANSFORM:
    case D3DOP_STATELIGHT:
    case D3DOP_STATERENDER: return sizeof(D3DSTATE);
    case D3DOP_PROCESSVERTICES: return sizeof(D3DPROCESSVERTICES);
    case D3DOP_TEXTURELOAD: return sizeof(D3DTEXTURELOAD);
    case D3DOP_EXIT: return 0;
    case D3DOP_BRANCHFORWARD: return sizeof(D3DBRANCH);
    case D3DOP_SPAN: return sizeof(D3DSPAN);
    case D3DOP_SETSTATUS: return sizeof(D3DSTATUS);
    default: return ~0u;
    }
}

static HRESULT d3d_execute_validate_internal(D3DLegacyExecuteBuffer *buffer,
        DWORD *bad_offset, LPD3DVALIDATECALLBACK callback, void *context)
{
    DWORD cursor = buffer->data.dwInstructionOffset;
    DWORD end = cursor + buffer->data.dwInstructionLength;
    BOOL found_exit = FALSE;
    while (cursor < end) {
        D3DINSTRUCTION *instruction;
        UINT expected;
        uint64_t next;
        if (end - cursor < sizeof(*instruction)) break;
        instruction = (D3DINSTRUCTION *)(buffer->memory + cursor);
        expected = d3d_execute_expected_size(instruction->bOpcode);
        next = (uint64_t)cursor + sizeof(*instruction) +
                (uint64_t)instruction->bSize * instruction->wCount;
        if (expected == ~0u || instruction->bSize < expected || next > end)
            break;
        cursor = (DWORD)next;
        if (instruction->bOpcode == D3DOP_EXIT) {
            found_exit = TRUE;
            break;
        }
    }
    if (found_exit) return D3D_OK;
    if (bad_offset) *bad_offset = cursor;
    if (callback) callback(context, cursor);
    return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
}

static HRESULT WINAPI d3d_execute_buffer_Validate(
        IDirect3DExecuteBuffer *iface, DWORD *offset,
        LPD3DVALIDATECALLBACK callback, void *context, DWORD reserved)
{
    D3DLegacyExecuteBuffer *buffer = d3d_execute_buffer_from_iface(iface);
    (void)reserved;
    if (buffer->locked) return DDWG_TRACE_ERROR(D3DERR_EXECUTE_LOCKED);
    return d3d_execute_validate_internal(buffer, offset, callback, context);
}

static HRESULT WINAPI d3d_execute_buffer_Optimize(
        IDirect3DExecuteBuffer *iface, DWORD dummy)
{
    (void)dummy;
    return d3d_execute_validate_internal(d3d_execute_buffer_from_iface(iface),
            NULL, NULL, NULL);
}

static HRESULT WINAPI d3d1_device_CreateExecuteBuffer(IDirect3DDevice *iface,
        D3DEXECUTEBUFFERDESC *description, IDirect3DExecuteBuffer **out,
        IUnknown *outer)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    D3DLegacyExecuteBuffer *buffer;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    if (!description || description->dwSize < sizeof(*description) ||
            !(description->dwFlags & D3DDEB_BUFSIZE) ||
            !description->dwBufferSize) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    buffer = (D3DLegacyExecuteBuffer *)heap_alloc_zero(sizeof(*buffer));
    if (!buffer) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    buffer->memory = (BYTE *)heap_alloc_zero(description->dwBufferSize);
    if (!buffer->memory) { heap_free(buffer); return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY); }
    if ((description->dwFlags & D3DDEB_LPDATA) && description->lpData)
        CopyMemory(buffer->memory, description->lpData,
                description->dwBufferSize);
    buffer->vtbl = &g_d3d_execute_buffer_vtbl;
    buffer->ref = 1;
    buffer->device = device;
    buffer->byte_count = description->dwBufferSize;
    buffer->caps = (description->dwFlags & D3DDEB_CAPS) ?
            description->dwCaps : D3DDEBCAPS_SYSTEMMEMORY;
    buffer->data.dwSize = sizeof(buffer->data);
    d3d7_device_AddRef((IDirect3DDevice7 *)device);
    *out = (IDirect3DExecuteBuffer *)buffer;
    return D3D_OK;
}

static HRESULT WINAPI d3d1_device_GetStats(IDirect3DDevice *iface,
        D3DSTATS *stats)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    return d3d3_device_GetStats((IDirect3DDevice3 *)&device->device3_vtbl,
            stats);
}

static D3DLegacyMatrix *d3d_legacy_find_matrix(D3D7Device *device,
        D3DMATRIXHANDLE handle)
{
    D3DLegacyMatrix *matrix;
    for (matrix = device->legacy_matrices; matrix; matrix = matrix->next) {
        if (matrix->handle == handle) return matrix;
    }
    return NULL;
}

static HRESULT WINAPI d3d1_device_CreateMatrix(IDirect3DDevice *iface,
        D3DMATRIXHANDLE *handle)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    D3DLegacyMatrix *matrix;
    if (!handle) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *handle = 0;
    matrix = (D3DLegacyMatrix *)heap_alloc_zero(sizeof(*matrix));
    if (!matrix) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    matrix->handle = allocate_handle();
    d3d7_identity_matrix(&matrix->matrix);
    matrix->next = device->legacy_matrices;
    device->legacy_matrices = matrix;
    *handle = matrix->handle;
    return D3D_OK;
}

static HRESULT WINAPI d3d1_device_SetMatrix(IDirect3DDevice *iface,
        D3DMATRIXHANDLE handle, D3DMATRIX *data)
{
    D3DLegacyMatrix *matrix = d3d_legacy_find_matrix(
            d3d1_device_from_iface(iface), handle);
    if (!matrix || !data) return DDWG_TRACE_ERROR(D3DERR_MATRIX_SETDATA_FAILED);
    matrix->matrix = *data;
    return D3D_OK;
}

static HRESULT WINAPI d3d1_device_GetMatrix(IDirect3DDevice *iface,
        D3DMATRIXHANDLE handle, D3DMATRIX *data)
{
    D3DLegacyMatrix *matrix = d3d_legacy_find_matrix(
            d3d1_device_from_iface(iface), handle);
    if (!matrix || !data) return DDWG_TRACE_ERROR(D3DERR_MATRIX_GETDATA_FAILED);
    *data = matrix->matrix;
    return D3D_OK;
}

static HRESULT WINAPI d3d1_device_DeleteMatrix(IDirect3DDevice *iface,
        D3DMATRIXHANDLE handle)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    D3DLegacyMatrix **link = &device->legacy_matrices;
    while (*link && (*link)->handle != handle) link = &(*link)->next;
    if (!*link) return DDWG_TRACE_ERROR(D3DERR_MATRIX_DESTROY_FAILED);
    {
        D3DLegacyMatrix *matrix = *link;
        *link = matrix->next;
        heap_free(matrix);
    }
    return D3D_OK;
}

static HRESULT d3d_execute_process_vertices(D3DLegacyExecuteBuffer *buffer,
        const D3DPROCESSVERTICES *process)
{
    D3D7Device *device = buffer->device;
    D3DMATRIX world_view, transform;
    DWORD index;
    DWORD operation = process->dwFlags & D3DPROCESSVERTICES_OPMASK;
    if ((DWORD)process->wStart > buffer->data.dwVertexCount ||
            process->dwCount > buffer->data.dwVertexCount - process->wStart ||
            (DWORD)process->wDest > buffer->processed_count ||
            process->dwCount > buffer->processed_count - process->wDest)
        return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
    if (operation == D3DPROCESSVERTICES_COPY) {
        uint64_t source_offset = (uint64_t)buffer->data.dwVertexOffset +
                (uint64_t)process->wStart * sizeof(D3DTLVERTEX);
        uint64_t source_end = source_offset +
                (uint64_t)process->dwCount * sizeof(D3DTLVERTEX);
        if (source_end > buffer->byte_count) return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
        CopyMemory(buffer->processed + process->wDest,
                buffer->memory + (DWORD)source_offset,
                process->dwCount * sizeof(D3DTLVERTEX));
        return D3D_OK;
    }
    if (operation != D3DPROCESSVERTICES_TRANSFORM &&
            operation != D3DPROCESSVERTICES_TRANSFORMLIGHT)
        return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
    d3d7_multiply_matrix(&device->transforms[D3DTRANSFORMSTATE_WORLD],
            &device->transforms[D3DTRANSFORMSTATE_VIEW], &world_view);
    d3d7_multiply_matrix(&world_view,
            &device->transforms[D3DTRANSFORMSTATE_PROJECTION], &transform);
    for (index = 0; index < process->dwCount; ++index) {
        const D3DVERTEX *source = (const D3DVERTEX *)(buffer->memory +
                buffer->data.dwVertexOffset) + process->wStart + index;
        D3DTLVERTEX *destination = buffer->processed + process->wDest + index;
        float position[4] = { source->x, source->y, source->z, 1.0f };
        float eye_position[4], clip[4];
        float reciprocal_w;
        d3d7_transform_vector(&world_view, position, eye_position);
        d3d7_transform_vector(&transform, position, clip);
        reciprocal_w = clip[3] != 0.0f ? 1.0f / clip[3] : 0.0f;
        destination->sx = device->viewport.dwX +
                (clip[0] * reciprocal_w + 1.0f) *
                (float)device->viewport.dwWidth * 0.5f;
        destination->sy = device->viewport.dwY +
                (1.0f - clip[1] * reciprocal_w) *
                (float)device->viewport.dwHeight * 0.5f;
        destination->sz = device->viewport.dvMinZ +
                clip[2] * reciprocal_w *
                (device->viewport.dvMaxZ - device->viewport.dvMinZ);
        destination->rhw = reciprocal_w;
        destination->tu = source->tu;
        destination->tv = source->tv;
        destination->specular = 0;
        if (operation == D3DPROCESSVERTICES_TRANSFORMLIGHT &&
                !(process->dwFlags & D3DPROCESSVERTICES_NOCOLOR)) {
            float normal_source[4] = { source->nx, source->ny, source->nz, 0.0f };
            float normal[4], color[4];
            float length;
            d3d7_transform_vector(&world_view, normal_source, normal);
            length = d3d7_sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                    normal[2] * normal[2]);
            if (length > 0.0f) {
                normal[0] /= length; normal[1] /= length; normal[2] /= length;
            }
            d3d7_light_vertex(device, eye_position, normal, color);
            destination->color = d3d7_pack_color(color);
        } else if (!(process->dwFlags & D3DPROCESSVERTICES_NOCOLOR)) {
            destination->color = 0xffffffffu;
        }
        if (process->dwFlags & D3DPROCESSVERTICES_UPDATEEXTENTS) {
            LONG x = (LONG)destination->sx, y = (LONG)destination->sy;
            D3DRECT *extent = &buffer->data.dsStatus.drExtent;
            if (!index && !process->wDest) {
                extent->x1 = extent->x2 = x;
                extent->y1 = extent->y2 = y;
            } else {
                if (x < extent->x1) extent->x1 = x;
                if (x > extent->x2) extent->x2 = x;
                if (y < extent->y1) extent->y1 = y;
                if (y > extent->y2) extent->y2 = y;
            }
        }
    }
    return D3D_OK;
}

static HRESULT d3d_execute_draw_indexed(D3DLegacyExecuteBuffer *buffer,
        D3DPRIMITIVETYPE primitive_type, const BYTE *records, BYTE size,
        WORD count, UINT vertices_per_record)
{
    D3D7Device *device = buffer->device;
    DWORD index_count = (DWORD)count * vertices_per_record;
    WORD *indices;
    DWORD index;
    if (!count) return D3D_OK;
    indices = (WORD *)heap_alloc_zero(index_count * sizeof(*indices));
    if (!indices) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    for (index = 0; index < count; ++index) {
        if (vertices_per_record == 2u) {
            const D3DLINE *line = (const D3DLINE *)(records + index * size);
            indices[index * 2u] = line->v1;
            indices[index * 2u + 1u] = line->v2;
            if (line->v1 >= buffer->processed_count ||
                    line->v2 >= buffer->processed_count) {
                heap_free(indices);
                return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
            }
        } else {
            const D3DTRIANGLE *triangle =
                    (const D3DTRIANGLE *)(records + index * size);
            indices[index * 3u] = triangle->v1;
            indices[index * 3u + 1u] = triangle->v2;
            indices[index * 3u + 2u] = triangle->v3;
            if (triangle->v1 >= buffer->processed_count ||
                    triangle->v2 >= buffer->processed_count ||
                    triangle->v3 >= buffer->processed_count) {
                heap_free(indices);
                return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
            }
        }
    }
    {
        HRESULT result = d3d7_device_DrawIndexedPrimitive(
                (IDirect3DDevice7 *)device, primitive_type, D3DFVF_TLVERTEX,
                buffer->processed, buffer->processed_count, indices,
                index_count, 0);
        heap_free(indices);
        return DDWG_TRACE_ERROR(result);
    }
}

static BOOL d3d_execute_pick_triangle(LONG x, LONG y,
        const D3DTLVERTEX *vertices)
{
    float area = (vertices[1].sx - vertices[0].sx) *
            (vertices[2].sy - vertices[0].sy) -
            (vertices[1].sy - vertices[0].sy) *
            (vertices[2].sx - vertices[0].sx);
    UINT edge;
    if (area == 0.0f) return FALSE;
    for (edge = 0; edge < 3u; ++edge) {
        const D3DTLVERTEX *first = &vertices[edge];
        const D3DTLVERTEX *second = &vertices[(edge + 1u) % 3u];
        float value = (second->sx - first->sx) * (y - first->sy) -
                (second->sy - first->sy) * (x - first->sx);
        BOOL top_left = (first->sy == second->sy && first->sx > second->sx) ||
                first->sy < second->sy;
        if (area > 0.0f) {
            if (value < 0.0f || (value == 0.0f && !top_left)) return FALSE;
        } else {
            if (value > 0.0f || (value == 0.0f && top_left)) return FALSE;
        }
    }
    return TRUE;
}

static float d3d_execute_pick_z(LONG x, LONG y,
        const D3DTLVERTEX *vertices)
{
    float denominator = (vertices[1].sy - vertices[2].sy) *
            (vertices[0].sx - vertices[2].sx) +
            (vertices[2].sx - vertices[1].sx) *
            (vertices[0].sy - vertices[2].sy);
    float first, second, third;
    if (denominator == 0.0f) return vertices[0].sz;
    first = ((vertices[1].sy - vertices[2].sy) *
            (x - vertices[2].sx) +
            (vertices[2].sx - vertices[1].sx) *
            (y - vertices[2].sy)) / denominator;
    second = ((vertices[2].sy - vertices[0].sy) *
            (x - vertices[2].sx) +
            (vertices[0].sx - vertices[2].sx) *
            (y - vertices[2].sy)) / denominator;
    third = 1.0f - first - second;
    return first * vertices[0].sz + second * vertices[1].sz +
            third * vertices[2].sz;
}

static HRESULT d3d_execute_add_pick_record(D3D7Device *device, BYTE opcode,
        DWORD offset, float z)
{
    D3DPICKRECORD *record;
    if (!d3d_legacy_grow((void **)&device->legacy_pick_records,
            &device->legacy_pick_record_capacity,
            device->legacy_pick_record_count + 1u, sizeof(*record)))
        return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    record = &device->legacy_pick_records[device->legacy_pick_record_count++];
    record->bOpcode = opcode;
    record->bPad = 0;
    record->dwOffset = offset;
    record->dvZ = z;
    return D3D_OK;
}

static HRESULT d3d_execute_run(D3DLegacyExecuteBuffer *buffer,
        const D3DRECT *pick_rect)
{
    D3D7Device *device = buffer->device;
    DWORD cursor = buffer->data.dwInstructionOffset;
    DWORD end = cursor + buffer->data.dwInstructionLength;
    DWORD steps = 0;
    HRESULT result;
    result = d3d_execute_validate_internal(buffer, NULL, NULL, NULL);
    if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    if (pick_rect) device->legacy_pick_record_count = 0;
    while (cursor < end && ++steps < 1000000u) {
        D3DINSTRUCTION *instruction =
                (D3DINSTRUCTION *)(buffer->memory + cursor);
        BYTE *records = (BYTE *)(instruction + 1);
        DWORD next = cursor + sizeof(*instruction) +
                instruction->bSize * instruction->wCount;
        DWORD index;
        switch (instruction->bOpcode) {
        case D3DOP_POINT:
            if (pick_rect) break;
            for (index = 0; index < instruction->wCount; ++index) {
                D3DPOINT *point = (D3DPOINT *)(records +
                        index * instruction->bSize);
                if ((DWORD)point->wFirst > buffer->processed_count ||
                        point->wCount > buffer->processed_count - point->wFirst)
                    return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
                if (!point->wCount) continue;
                result = d3d7_device_DrawPrimitive((IDirect3DDevice7 *)device,
                        D3DPT_POINTLIST, D3DFVF_TLVERTEX,
                        buffer->processed + point->wFirst, point->wCount, 0);
                if (FAILED(result)) return DDWG_TRACE_ERROR(result);
            }
            break;
        case D3DOP_LINE:
            if (pick_rect) break;
            result = d3d_execute_draw_indexed(buffer, D3DPT_LINELIST, records,
                    instruction->bSize, instruction->wCount, 2u);
            if (FAILED(result)) return DDWG_TRACE_ERROR(result);
            break;
        case D3DOP_TRIANGLE:
            if (pick_rect) {
                for (index = 0; index < instruction->wCount; ++index) {
                    D3DTRIANGLE *triangle = (D3DTRIANGLE *)(records +
                            index * instruction->bSize);
                    D3DTLVERTEX vertices[3];
                    if (triangle->v1 >= buffer->processed_count ||
                            triangle->v2 >= buffer->processed_count ||
                            triangle->v3 >= buffer->processed_count)
                        return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
                    vertices[0] = buffer->processed[triangle->v1];
                    vertices[1] = buffer->processed[triangle->v2];
                    vertices[2] = buffer->processed[triangle->v3];
                    if (d3d_execute_pick_triangle(pick_rect->x1,
                            pick_rect->y1, vertices)) {
                        result = d3d_execute_add_pick_record(device,
                                D3DOP_TRIANGLE,
                                (DWORD)(records + index * instruction->bSize -
                                buffer->memory -
                                buffer->data.dwInstructionOffset),
                                d3d_execute_pick_z(pick_rect->x1,
                                pick_rect->y1, vertices));
                        if (FAILED(result)) return DDWG_TRACE_ERROR(result);
                        break;
                    }
                }
                break;
            }
            result = d3d_execute_draw_indexed(buffer, D3DPT_TRIANGLELIST,
                    records, instruction->bSize, instruction->wCount, 3u);
            if (FAILED(result)) return DDWG_TRACE_ERROR(result);
            break;
        case D3DOP_MATRIXLOAD:
            for (index = 0; index < instruction->wCount; ++index) {
                D3DMATRIXLOAD *load = (D3DMATRIXLOAD *)(records +
                        index * instruction->bSize);
                D3DLegacyMatrix *destination = d3d_legacy_find_matrix(device,
                        load->hDestMatrix);
                D3DLegacyMatrix *source = d3d_legacy_find_matrix(device,
                        load->hSrcMatrix);
                if (!destination || !source) return DDWG_TRACE_ERROR(D3DERR_INVALIDMATRIX);
                destination->matrix = source->matrix;
            }
            break;
        case D3DOP_MATRIXMULTIPLY:
            for (index = 0; index < instruction->wCount; ++index) {
                D3DMATRIXMULTIPLY *multiply = (D3DMATRIXMULTIPLY *)(records +
                        index * instruction->bSize);
                D3DLegacyMatrix *destination = d3d_legacy_find_matrix(device,
                        multiply->hDestMatrix);
                D3DLegacyMatrix *first = d3d_legacy_find_matrix(device,
                        multiply->hSrcMatrix1);
                D3DLegacyMatrix *second = d3d_legacy_find_matrix(device,
                        multiply->hSrcMatrix2);
                D3DMATRIX value;
                if (!destination || !first || !second)
                    return DDWG_TRACE_ERROR(D3DERR_INVALIDMATRIX);
                d3d7_multiply_matrix(&first->matrix, &second->matrix, &value);
                destination->matrix = value;
            }
            break;
        case D3DOP_STATETRANSFORM:
            for (index = 0; index < instruction->wCount; ++index) {
                D3DSTATE *state = (D3DSTATE *)(records +
                        index * instruction->bSize);
                D3DLegacyMatrix *matrix = d3d_legacy_find_matrix(device,
                        state->dwArg[0]);
                if (!matrix) return DDWG_TRACE_ERROR(D3DERR_INVALIDMATRIX);
                result = d3d7_device_SetTransform((IDirect3DDevice7 *)device,
                        state->dtstTransformStateType, &matrix->matrix);
                if (FAILED(result)) return DDWG_TRACE_ERROR(result);
            }
            break;
        case D3DOP_STATELIGHT:
            for (index = 0; index < instruction->wCount; ++index) {
                D3DSTATE *state = (D3DSTATE *)(records +
                        index * instruction->bSize);
                result = d3d3_device_SetLightState((IDirect3DDevice3 *)&
                        device->device3_vtbl, state->dlstLightStateType,
                        state->dwArg[0]);
                if (FAILED(result)) return DDWG_TRACE_ERROR(result);
            }
            break;
        case D3DOP_STATERENDER:
            for (index = 0; index < instruction->wCount; ++index) {
                D3DSTATE *state = (D3DSTATE *)(records +
                        index * instruction->bSize);
                result = d3d3_device_SetRenderState((IDirect3DDevice3 *)&
                        device->device3_vtbl, state->drstRenderStateType,
                        state->dwArg[0]);
                if (FAILED(result)) return DDWG_TRACE_ERROR(result);
            }
            break;
        case D3DOP_PROCESSVERTICES:
            for (index = 0; index < instruction->wCount; ++index) {
                result = d3d_execute_process_vertices(buffer,
                        (D3DPROCESSVERTICES *)(records +
                        index * instruction->bSize));
                if (FAILED(result)) return DDWG_TRACE_ERROR(result);
            }
            break;
        case D3DOP_TEXTURELOAD:
            for (index = 0; index < instruction->wCount; ++index) {
                D3DTEXTURELOAD *load = (D3DTEXTURELOAD *)(records +
                        index * instruction->bSize);
                DDSurface *destination = d3d_legacy_find_texture(device->owner,
                        load->hDestTexture);
                DDSurface *source = d3d_legacy_find_texture(device->owner,
                        load->hSrcTexture);
                if (!destination || !source) return DDWG_TRACE_ERROR(D3DERR_TEXTURE_LOAD_FAILED);
                result = d3d_legacy_texture_load_surface(destination, source);
                if (FAILED(result)) return DDWG_TRACE_ERROR(result);
            }
            break;
        case D3DOP_EXIT:
            return D3D_OK;
        case D3DOP_BRANCHFORWARD:
            for (index = 0; index < instruction->wCount; ++index) {
                D3DBRANCH *branch = (D3DBRANCH *)(records +
                        index * instruction->bSize);
                BOOL equal = (buffer->data.dsStatus.dwStatus & branch->dwMask)
                        == branch->dwValue;
                if (equal != !!branch->bNegate && branch->dwOffset) {
                    uint64_t target = (uint64_t)cursor + branch->dwOffset;
                    if (target <= cursor ||
                            target >= end) return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
                    next = (DWORD)target;
                    break;
                }
            }
            break;
        case D3DOP_SPAN:
            if (pick_rect) break;
            for (index = 0; index < instruction->wCount; ++index) {
                D3DSPAN *span = (D3DSPAN *)(records +
                        index * instruction->bSize);
                if ((DWORD)span->wFirst > buffer->processed_count ||
                        span->wCount > buffer->processed_count - span->wFirst)
                    return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
                if (!span->wCount) continue;
                result = d3d7_device_DrawPrimitive((IDirect3DDevice7 *)device,
                        D3DPT_POINTLIST, D3DFVF_TLVERTEX,
                        buffer->processed + span->wFirst, span->wCount, 0);
                if (FAILED(result)) return DDWG_TRACE_ERROR(result);
            }
            break;
        case D3DOP_SETSTATUS:
            for (index = 0; index < instruction->wCount; ++index) {
                D3DSTATUS *status = (D3DSTATUS *)(records +
                        index * instruction->bSize);
                if (status->dwFlags & D3DSETSTATUS_STATUS)
                    buffer->data.dsStatus.dwStatus = status->dwStatus;
                if (status->dwFlags & D3DSETSTATUS_EXTENTS)
                    buffer->data.dsStatus.drExtent = status->drExtent;
                buffer->data.dsStatus.dwFlags = status->dwFlags;
            }
            break;
        default:
            return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
        }
        cursor = next;
    }
    return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
}

static HRESULT WINAPI d3d1_device_Execute(IDirect3DDevice *iface,
        IDirect3DExecuteBuffer *buffer_iface, IDirect3DViewport *viewport_iface,
        DWORD flags)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    D3DLegacyExecuteBuffer *buffer;
    D3DLegacyViewport *viewport;
    HRESULT result;
    if (!buffer_iface || buffer_iface->lpVtbl != &g_d3d_execute_buffer_vtbl ||
            !viewport_iface || (flags != D3DEXECUTE_CLIPPED &&
            flags != D3DEXECUTE_UNCLIPPED)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    buffer = d3d_execute_buffer_from_iface(buffer_iface);
    viewport = d3d_viewport_from_any(viewport_iface);
    if (buffer->device != device || buffer->locked || viewport->device != device)
        return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
    result = d3d_legacy_set_current_viewport(device, viewport);
    if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    return d3d_execute_run(buffer, NULL);
}

static HRESULT WINAPI d3d1_device_AddViewport(IDirect3DDevice *iface,
        IDirect3DViewport *viewport)
{
    return d3d_legacy_add_viewport(d3d1_device_from_iface(iface),
            viewport ? d3d_viewport_from_any(viewport) : NULL);
}

static HRESULT WINAPI d3d1_device_DeleteViewport(IDirect3DDevice *iface,
        IDirect3DViewport *viewport)
{
    return d3d_legacy_delete_viewport(d3d1_device_from_iface(iface),
            viewport ? d3d_viewport_from_any(viewport) : NULL);
}

static HRESULT WINAPI d3d1_device_NextViewport(IDirect3DDevice *iface,
        IDirect3DViewport *reference, IDirect3DViewport **out, DWORD flags)
{
    D3DLegacyViewport *viewport;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    viewport = d3d_legacy_next_viewport(d3d1_device_from_iface(iface),
            reference ? d3d_viewport_from_any(reference) : NULL, flags);
    if (!viewport) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
    *out = (IDirect3DViewport *)&viewport->vtbl1;
    d3d_viewport_AddRef((IDirect3DViewport3 *)&viewport->vtbl3);
    return D3D_OK;
}

static HRESULT WINAPI d3d1_device_Pick(IDirect3DDevice *iface,
        IDirect3DExecuteBuffer *buffer, IDirect3DViewport *viewport,
        DWORD flags, D3DRECT *rect)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    D3DLegacyExecuteBuffer *execute_buffer;
    D3DLegacyViewport *legacy_viewport;
    HRESULT result;
    if (!buffer || buffer->lpVtbl != &g_d3d_execute_buffer_vtbl || !viewport ||
            !rect || (flags != D3DEXECUTE_CLIPPED &&
            flags != D3DEXECUTE_UNCLIPPED)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    execute_buffer = d3d_execute_buffer_from_iface(buffer);
    legacy_viewport = d3d_viewport_from_any(viewport);
    if (execute_buffer->device != device || execute_buffer->locked ||
            legacy_viewport->device != device) return DDWG_TRACE_ERROR(D3DERR_EXECUTE_FAILED);
    result = d3d_legacy_set_current_viewport(device, legacy_viewport);
    if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    return d3d_execute_run(execute_buffer, rect);
}

static HRESULT WINAPI d3d1_device_GetPickRecords(IDirect3DDevice *iface,
        DWORD *count, D3DPICKRECORD *records)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    if (!count) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *count = device->legacy_pick_record_count;
    if (records && *count)
        CopyMemory(records, device->legacy_pick_records,
                *count * sizeof(*records));
    return D3D_OK;
}

static HRESULT WINAPI d3d1_device_EnumTextureFormats(IDirect3DDevice *iface,
        LPD3DENUMTEXTUREFORMATSCALLBACK callback, void *context)
{
    D3DLegacyTextureEnumContext adapter;
    (void)iface;
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    adapter.callback = callback;
    adapter.context = context;
    return d3d7_enum_texture_formats(d3d_legacy_texture_enum_callback,
            &adapter, FALSE);
}

static HRESULT WINAPI d3d1_device_BeginScene(IDirect3DDevice *iface)
{
    return d3d7_device_BeginScene((IDirect3DDevice7 *)
            d3d1_device_from_iface(iface));
}

static HRESULT WINAPI d3d1_device_EndScene(IDirect3DDevice *iface)
{
    return d3d7_device_EndScene((IDirect3DDevice7 *)
            d3d1_device_from_iface(iface));
}

static HRESULT WINAPI d3d1_device_GetDirect3D(IDirect3DDevice *iface,
        IDirect3D **out)
{
    D3D7Device *device = d3d1_device_from_iface(iface);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = (IDirect3D *)&device->owner->d3d1_vtbl;
    d3d1_AddRef(*out);
    return D3D_OK;
}

static void d3d_legacy_device_cleanup(D3D7Device *device)
{
    D3DLegacyViewport *viewport = device->legacy_viewports;
    D3DLegacyMatrix *matrix = device->legacy_matrices;
    while (viewport) {
        D3DLegacyViewport *next = viewport->next_in_device;
        viewport->device = NULL;
        viewport->next_in_device = NULL;
        d3d_viewport_Release((IDirect3DViewport3 *)&viewport->vtbl3);
        viewport = next;
    }
    while (matrix) {
        D3DLegacyMatrix *next = matrix->next;
        heap_free(matrix);
        matrix = next;
    }
    if (device->legacy_material)
        d3d_material_Release((IDirect3DMaterial3 *)&
                device->legacy_material->vtbl3);
    heap_free(device->legacy_vertices);
    heap_free(device->legacy_indices);
    heap_free(device->legacy_pick_records);
}

/* Exact-signature thunks for inherited COM vtables. */
static HRESULT WINAPI d3d_material2_QueryInterface(IDirect3DMaterial2 *iface,
        REFIID iid, void **out)
{ return d3d_material_QueryInterface((IDirect3DMaterial3 *)iface, iid, out); }
static ULONG WINAPI d3d_material2_AddRef(IDirect3DMaterial2 *iface)
{ return d3d_material_AddRef((IDirect3DMaterial3 *)iface); }
static ULONG WINAPI d3d_material2_Release(IDirect3DMaterial2 *iface)
{ return d3d_material_Release((IDirect3DMaterial3 *)iface); }
static HRESULT WINAPI d3d_material2_SetMaterial(IDirect3DMaterial2 *iface,
        D3DMATERIAL *data)
{ return d3d_material_SetMaterial((IDirect3DMaterial3 *)iface, data); }
static HRESULT WINAPI d3d_material2_GetMaterial(IDirect3DMaterial2 *iface,
        D3DMATERIAL *data)
{ return d3d_material_GetMaterial((IDirect3DMaterial3 *)iface, data); }
static HRESULT WINAPI d3d_material1_QueryInterface(IDirect3DMaterial *iface,
        REFIID iid, void **out)
{ return d3d_material_QueryInterface((IDirect3DMaterial3 *)iface, iid, out); }
static ULONG WINAPI d3d_material1_AddRef(IDirect3DMaterial *iface)
{ return d3d_material_AddRef((IDirect3DMaterial3 *)iface); }
static ULONG WINAPI d3d_material1_Release(IDirect3DMaterial *iface)
{ return d3d_material_Release((IDirect3DMaterial3 *)iface); }
static HRESULT WINAPI d3d_material1_SetMaterial(IDirect3DMaterial *iface,
        D3DMATERIAL *data)
{ return d3d_material_SetMaterial((IDirect3DMaterial3 *)iface, data); }
static HRESULT WINAPI d3d_material1_GetMaterial(IDirect3DMaterial *iface,
        D3DMATERIAL *data)
{ return d3d_material_GetMaterial((IDirect3DMaterial3 *)iface, data); }

static HRESULT WINAPI d3d_viewport2_QueryInterface(IDirect3DViewport2 *iface,
        REFIID iid, void **out)
{ return d3d_viewport_QueryInterface((IDirect3DViewport3 *)iface, iid, out); }
static ULONG WINAPI d3d_viewport2_AddRef(IDirect3DViewport2 *iface)
{ return d3d_viewport_AddRef((IDirect3DViewport3 *)iface); }
static ULONG WINAPI d3d_viewport2_Release(IDirect3DViewport2 *iface)
{ return d3d_viewport_Release((IDirect3DViewport3 *)iface); }
static HRESULT WINAPI d3d_viewport2_Initialize(IDirect3DViewport2 *iface,
        IDirect3D *d3d)
{ return d3d_viewport_Initialize((IDirect3DViewport3 *)iface, d3d); }
static HRESULT WINAPI d3d_viewport2_GetViewport(IDirect3DViewport2 *iface,
        D3DVIEWPORT *data)
{ return d3d_viewport_GetViewport((IDirect3DViewport3 *)iface, data); }
static HRESULT WINAPI d3d_viewport2_SetViewport(IDirect3DViewport2 *iface,
        D3DVIEWPORT *data)
{ return d3d_viewport_SetViewport((IDirect3DViewport3 *)iface, data); }
static HRESULT WINAPI d3d_viewport2_TransformVertices(
        IDirect3DViewport2 *iface, DWORD count, D3DTRANSFORMDATA *data,
        DWORD flags, DWORD *offscreen)
{ return d3d_viewport_TransformVertices((IDirect3DViewport3 *)iface, count,
        data, flags, offscreen); }
static HRESULT WINAPI d3d_viewport2_LightElements(IDirect3DViewport2 *iface,
        DWORD count, D3DLIGHTDATA *data)
{ return d3d_viewport_LightElements((IDirect3DViewport3 *)iface, count, data); }
static HRESULT WINAPI d3d_viewport2_SetBackground(IDirect3DViewport2 *iface,
        D3DMATERIALHANDLE handle)
{ return d3d_viewport_SetBackground((IDirect3DViewport3 *)iface, handle); }
static HRESULT WINAPI d3d_viewport2_GetBackground(IDirect3DViewport2 *iface,
        D3DMATERIALHANDLE *handle, WINBOOL *valid)
{ return d3d_viewport_GetBackground((IDirect3DViewport3 *)iface, handle, valid); }
static HRESULT WINAPI d3d_viewport2_SetBackgroundDepth(
        IDirect3DViewport2 *iface, IDirectDrawSurface *surface)
{ return d3d_viewport_SetBackgroundDepth((IDirect3DViewport3 *)iface, surface); }
static HRESULT WINAPI d3d_viewport2_GetBackgroundDepth(
        IDirect3DViewport2 *iface, IDirectDrawSurface **surface, WINBOOL *valid)
{ return d3d_viewport_GetBackgroundDepth((IDirect3DViewport3 *)iface, surface,
        valid); }
static HRESULT WINAPI d3d_viewport2_Clear(IDirect3DViewport2 *iface,
        DWORD count, D3DRECT *rects, DWORD flags)
{ return d3d_viewport_Clear((IDirect3DViewport3 *)iface, count, rects, flags); }
static HRESULT WINAPI d3d_viewport2_AddLight(IDirect3DViewport2 *iface,
        IDirect3DLight *light)
{ return d3d_viewport_AddLight((IDirect3DViewport3 *)iface, light); }
static HRESULT WINAPI d3d_viewport2_DeleteLight(IDirect3DViewport2 *iface,
        IDirect3DLight *light)
{ return d3d_viewport_DeleteLight((IDirect3DViewport3 *)iface, light); }
static HRESULT WINAPI d3d_viewport2_NextLight(IDirect3DViewport2 *iface,
        IDirect3DLight *reference, IDirect3DLight **out, DWORD flags)
{ return d3d_viewport_NextLight((IDirect3DViewport3 *)iface, reference, out,
        flags); }
static HRESULT WINAPI d3d_viewport2_GetViewport2(IDirect3DViewport2 *iface,
        D3DVIEWPORT2 *data)
{ return d3d_viewport_GetViewport2((IDirect3DViewport3 *)iface, data); }
static HRESULT WINAPI d3d_viewport2_SetViewport2(IDirect3DViewport2 *iface,
        D3DVIEWPORT2 *data)
{ return d3d_viewport_SetViewport2((IDirect3DViewport3 *)iface, data); }

static HRESULT WINAPI d3d_viewport1_QueryInterface(IDirect3DViewport *iface,
        REFIID iid, void **out)
{ return d3d_viewport_QueryInterface((IDirect3DViewport3 *)iface, iid, out); }
static ULONG WINAPI d3d_viewport1_AddRef(IDirect3DViewport *iface)
{ return d3d_viewport_AddRef((IDirect3DViewport3 *)iface); }
static ULONG WINAPI d3d_viewport1_Release(IDirect3DViewport *iface)
{ return d3d_viewport_Release((IDirect3DViewport3 *)iface); }
static HRESULT WINAPI d3d_viewport1_Initialize(IDirect3DViewport *iface,
        IDirect3D *d3d)
{ return d3d_viewport_Initialize((IDirect3DViewport3 *)iface, d3d); }
static HRESULT WINAPI d3d_viewport1_GetViewport(IDirect3DViewport *iface,
        D3DVIEWPORT *data)
{ return d3d_viewport_GetViewport((IDirect3DViewport3 *)iface, data); }
static HRESULT WINAPI d3d_viewport1_SetViewport(IDirect3DViewport *iface,
        D3DVIEWPORT *data)
{ return d3d_viewport_SetViewport((IDirect3DViewport3 *)iface, data); }
static HRESULT WINAPI d3d_viewport1_TransformVertices(
        IDirect3DViewport *iface, DWORD count, D3DTRANSFORMDATA *data,
        DWORD flags, DWORD *offscreen)
{ return d3d_viewport_TransformVertices((IDirect3DViewport3 *)iface, count,
        data, flags, offscreen); }
static HRESULT WINAPI d3d_viewport1_LightElements(IDirect3DViewport *iface,
        DWORD count, D3DLIGHTDATA *data)
{ return d3d_viewport_LightElements((IDirect3DViewport3 *)iface, count, data); }
static HRESULT WINAPI d3d_viewport1_SetBackground(IDirect3DViewport *iface,
        D3DMATERIALHANDLE handle)
{ return d3d_viewport_SetBackground((IDirect3DViewport3 *)iface, handle); }
static HRESULT WINAPI d3d_viewport1_GetBackground(IDirect3DViewport *iface,
        D3DMATERIALHANDLE *handle, WINBOOL *valid)
{ return d3d_viewport_GetBackground((IDirect3DViewport3 *)iface, handle, valid); }
static HRESULT WINAPI d3d_viewport1_SetBackgroundDepth(
        IDirect3DViewport *iface, IDirectDrawSurface *surface)
{ return d3d_viewport_SetBackgroundDepth((IDirect3DViewport3 *)iface, surface); }
static HRESULT WINAPI d3d_viewport1_GetBackgroundDepth(
        IDirect3DViewport *iface, IDirectDrawSurface **surface, WINBOOL *valid)
{ return d3d_viewport_GetBackgroundDepth((IDirect3DViewport3 *)iface, surface,
        valid); }
static HRESULT WINAPI d3d_viewport1_Clear(IDirect3DViewport *iface,
        DWORD count, D3DRECT *rects, DWORD flags)
{ return d3d_viewport_Clear((IDirect3DViewport3 *)iface, count, rects, flags); }
static HRESULT WINAPI d3d_viewport1_AddLight(IDirect3DViewport *iface,
        IDirect3DLight *light)
{ return d3d_viewport_AddLight((IDirect3DViewport3 *)iface, light); }
static HRESULT WINAPI d3d_viewport1_DeleteLight(IDirect3DViewport *iface,
        IDirect3DLight *light)
{ return d3d_viewport_DeleteLight((IDirect3DViewport3 *)iface, light); }
static HRESULT WINAPI d3d_viewport1_NextLight(IDirect3DViewport *iface,
        IDirect3DLight *reference, IDirect3DLight **out, DWORD flags)
{ return d3d_viewport_NextLight((IDirect3DViewport3 *)iface, reference, out,
        flags); }

static IDirect3D3Vtbl g_d3d3_vtbl = {
    d3d3_QueryInterface, d3d3_AddRef, d3d3_Release,
    d3d3_EnumDevices, d3d3_CreateLight, d3d3_CreateMaterial,
    d3d3_CreateViewport, d3d3_FindDevice, d3d3_CreateDevice,
    d3d3_CreateVertexBuffer, d3d3_EnumZBufferFormats,
    d3d3_EvictManagedTextures,
};

static IDirect3D2Vtbl g_d3d2_vtbl = {
    d3d2_QueryInterface, d3d2_AddRef, d3d2_Release,
    d3d2_EnumDevices, d3d2_CreateLight, d3d2_CreateMaterial,
    d3d2_CreateViewport, d3d2_FindDevice, d3d2_CreateDevice,
};

static IDirect3DVtbl g_d3d1_vtbl = {
    d3d1_QueryInterface, d3d1_AddRef, d3d1_Release, d3d1_Initialize,
    d3d1_EnumDevices, d3d1_CreateLight, d3d1_CreateMaterial,
    d3d1_CreateViewport, d3d1_FindDevice,
};

static IDirect3DDevice3Vtbl g_d3d3_device_vtbl = {
    d3d3_device_QueryInterface, d3d3_device_AddRef, d3d3_device_Release,
    d3d3_device_GetCaps, d3d3_device_GetStats,
    d3d3_device_AddViewport, d3d3_device_DeleteViewport,
    d3d3_device_NextViewport, d3d3_device_EnumTextureFormats,
    d3d3_device_BeginScene, d3d3_device_EndScene,
    d3d3_device_GetDirect3D, d3d3_device_SetCurrentViewport,
    d3d3_device_GetCurrentViewport, d3d3_device_SetRenderTarget,
    d3d3_device_GetRenderTarget, d3d3_device_Begin,
    d3d3_device_BeginIndexed, d3d3_device_Vertex, d3d3_device_Index,
    d3d3_device_End, d3d3_device_GetRenderState,
    d3d3_device_SetRenderState, d3d3_device_GetLightState,
    d3d3_device_SetLightState, d3d3_device_SetTransform,
    d3d3_device_GetTransform, d3d3_device_MultiplyTransform,
    d3d3_device_DrawPrimitive, d3d3_device_DrawIndexedPrimitive,
    d3d3_device_SetClipStatus, d3d3_device_GetClipStatus,
    d3d3_device_DrawPrimitiveStrided,
    d3d3_device_DrawIndexedPrimitiveStrided,
    d3d3_device_DrawPrimitiveVB, d3d3_device_DrawIndexedPrimitiveVB,
    d3d3_device_ComputeSphereVisibility, d3d3_device_GetTexture,
    d3d3_device_SetTexture, d3d3_device_GetTextureStageState,
    d3d3_device_SetTextureStageState, d3d3_device_ValidateDevice,
};

static IDirect3DDevice2Vtbl g_d3d2_device_vtbl = {
    d3d2_device_QueryInterface, d3d2_device_AddRef, d3d2_device_Release,
    d3d2_device_GetCaps, d3d2_device_SwapTextureHandles,
    d3d2_device_GetStats, d3d2_device_AddViewport,
    d3d2_device_DeleteViewport, d3d2_device_NextViewport,
    d3d2_device_EnumTextureFormats, d3d2_device_BeginScene,
    d3d2_device_EndScene, d3d2_device_GetDirect3D,
    d3d2_device_SetCurrentViewport, d3d2_device_GetCurrentViewport,
    d3d2_device_SetRenderTarget, d3d2_device_GetRenderTarget,
    d3d2_device_Begin, d3d2_device_BeginIndexed, d3d2_device_Vertex,
    d3d2_device_Index, d3d2_device_End, d3d2_device_GetRenderState,
    d3d2_device_SetRenderState, d3d2_device_GetLightState,
    d3d2_device_SetLightState, d3d2_device_SetTransform,
    d3d2_device_GetTransform, d3d2_device_MultiplyTransform,
    d3d2_device_DrawPrimitive, d3d2_device_DrawIndexedPrimitive,
    d3d2_device_SetClipStatus, d3d2_device_GetClipStatus,
};

static IDirect3DDeviceVtbl g_d3d1_device_vtbl = {
    d3d1_device_QueryInterface, d3d1_device_AddRef, d3d1_device_Release,
    d3d1_device_Initialize, d3d1_device_GetCaps,
    d3d1_device_SwapTextureHandles, d3d1_device_CreateExecuteBuffer,
    d3d1_device_GetStats, d3d1_device_Execute, d3d1_device_AddViewport,
    d3d1_device_DeleteViewport, d3d1_device_NextViewport,
    d3d1_device_Pick, d3d1_device_GetPickRecords,
    d3d1_device_EnumTextureFormats, d3d1_device_CreateMatrix,
    d3d1_device_SetMatrix, d3d1_device_GetMatrix,
    d3d1_device_DeleteMatrix, d3d1_device_BeginScene,
    d3d1_device_EndScene, d3d1_device_GetDirect3D,
};

static IDirect3DVertexBufferVtbl g_d3d_vb_vtbl = {
    d3d_vb_QueryInterface, d3d_vb_AddRef, d3d_vb_Release,
    d3d_vb_Lock, d3d_vb_Unlock, d3d_vb_ProcessVertices,
    d3d_vb_GetVertexBufferDesc, d3d_vb_Optimize,
};

static IDirect3DTexture2Vtbl g_d3d_texture2_vtbl = {
    d3d_texture2_QueryInterface, d3d_texture2_AddRef,
    d3d_texture2_Release, d3d_texture2_GetHandle,
    d3d_texture2_PaletteChanged, d3d_texture2_Load,
};

static IDirect3DTextureVtbl g_d3d_texture1_vtbl = {
    d3d_texture1_QueryInterface, d3d_texture1_AddRef,
    d3d_texture1_Release, d3d_texture1_Initialize,
    d3d_texture1_GetHandle, d3d_texture1_PaletteChanged,
    d3d_texture1_Load, d3d_texture1_Unload,
};

static IDirect3DMaterial3Vtbl g_d3d_material3_vtbl = {
    d3d_material_QueryInterface, d3d_material_AddRef, d3d_material_Release,
    d3d_material_SetMaterial, d3d_material_GetMaterial,
    d3d_material3_GetHandle,
};

static IDirect3DMaterial2Vtbl g_d3d_material2_vtbl = {
    d3d_material2_QueryInterface, d3d_material2_AddRef,
    d3d_material2_Release, d3d_material2_SetMaterial,
    d3d_material2_GetMaterial, d3d_material2_GetHandle,
};

static IDirect3DMaterialVtbl g_d3d_material1_vtbl = {
    d3d_material1_QueryInterface, d3d_material1_AddRef,
    d3d_material1_Release, d3d_material1_Initialize,
    d3d_material1_SetMaterial, d3d_material1_GetMaterial,
    d3d_material1_GetHandle, d3d_material1_Reserve,
    d3d_material1_Unreserve,
};

static IDirect3DLightVtbl g_d3d_light_vtbl = {
    d3d_light_QueryInterface, d3d_light_AddRef, d3d_light_Release,
    d3d_light_Initialize, d3d_light_SetLight, d3d_light_GetLight,
};

static IDirect3DViewport3Vtbl g_d3d_viewport3_vtbl = {
    d3d_viewport_QueryInterface, d3d_viewport_AddRef, d3d_viewport_Release,
    d3d_viewport_Initialize, d3d_viewport_GetViewport,
    d3d_viewport_SetViewport, d3d_viewport_TransformVertices,
    d3d_viewport_LightElements, d3d_viewport_SetBackground,
    d3d_viewport_GetBackground, d3d_viewport_SetBackgroundDepth,
    d3d_viewport_GetBackgroundDepth, d3d_viewport_Clear,
    d3d_viewport_AddLight, d3d_viewport_DeleteLight,
    d3d_viewport_NextLight, d3d_viewport_GetViewport2,
    d3d_viewport_SetViewport2, d3d_viewport_SetBackgroundDepth2,
    d3d_viewport_GetBackgroundDepth2, d3d_viewport_Clear2,
};

static IDirect3DViewport2Vtbl g_d3d_viewport2_vtbl = {
    d3d_viewport2_QueryInterface, d3d_viewport2_AddRef,
    d3d_viewport2_Release, d3d_viewport2_Initialize,
    d3d_viewport2_GetViewport, d3d_viewport2_SetViewport,
    d3d_viewport2_TransformVertices, d3d_viewport2_LightElements,
    d3d_viewport2_SetBackground, d3d_viewport2_GetBackground,
    d3d_viewport2_SetBackgroundDepth, d3d_viewport2_GetBackgroundDepth,
    d3d_viewport2_Clear, d3d_viewport2_AddLight,
    d3d_viewport2_DeleteLight, d3d_viewport2_NextLight,
    d3d_viewport2_GetViewport2, d3d_viewport2_SetViewport2,
};

static IDirect3DViewportVtbl g_d3d_viewport1_vtbl = {
    d3d_viewport1_QueryInterface, d3d_viewport1_AddRef,
    d3d_viewport1_Release, d3d_viewport1_Initialize,
    d3d_viewport1_GetViewport, d3d_viewport1_SetViewport,
    d3d_viewport1_TransformVertices, d3d_viewport1_LightElements,
    d3d_viewport1_SetBackground, d3d_viewport1_GetBackground,
    d3d_viewport1_SetBackgroundDepth, d3d_viewport1_GetBackgroundDepth,
    d3d_viewport1_Clear, d3d_viewport1_AddLight,
    d3d_viewport1_DeleteLight, d3d_viewport1_NextLight,
};

static IDirect3DExecuteBufferVtbl g_d3d_execute_buffer_vtbl = {
    d3d_execute_buffer_QueryInterface, d3d_execute_buffer_AddRef,
    d3d_execute_buffer_Release, d3d_execute_buffer_Initialize,
    d3d_execute_buffer_Lock, d3d_execute_buffer_Unlock,
    d3d_execute_buffer_SetExecuteData, d3d_execute_buffer_GetExecuteData,
    d3d_execute_buffer_Validate, d3d_execute_buffer_Optimize,
};
