static HRESULT WINAPI device_create_vertex_buffer(IDirect3DDevice8 *iface,
        UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
        IDirect3DVertexBuffer8 **buffer_out)
{
    D8Device *device = device_from_iface(iface);
    D8VertexBuffer *buffer;
    if (!buffer_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *buffer_out = NULL;
    if (!length || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    buffer = (D8VertexBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*buffer));
    if (!buffer)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    buffer->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            length);
    if (!buffer->shadow) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    buffer->iface.lpVtbl = &g_vb_vtbl;
    buffer->refcount = 1;
    buffer->device = device;
    device_child_add_ref(device);
    buffer->handle = allocate_handle();
    buffer->length = length;
    buffer->usage = usage;
    buffer->fvf = fvf;
    buffer->pool = pool;

    if (!emit_vertex_buffer_create(device, buffer)) {
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        HeapFree(GetProcessHeap(), 0, buffer);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    buffer->next_device_resource = device->vertex_buffers;
    device->vertex_buffers = buffer;
    *buffer_out = &buffer->iface;
    return D3D_OK;
}

static HRESULT WINAPI device_create_index_buffer(IDirect3DDevice8 *iface,
        UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DIndexBuffer8 **buffer_out)
{
    D8Device *device = device_from_iface(iface);
    D8IndexBuffer *buffer;
    UINT index_size;

    if (!buffer_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *buffer_out = NULL;
    if (!length || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (format == D3DFMT_INDEX16)
        index_size = 2;
    else if (format == D3DFMT_INDEX32)
        index_size = 4;
    else
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (length % index_size)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    buffer = (D8IndexBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*buffer));
    if (!buffer)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    buffer->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            length);
    if (!buffer->shadow) {
        HeapFree(GetProcessHeap(), 0, buffer);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    buffer->iface.lpVtbl = &g_ib_vtbl;
    buffer->refcount = 1;
    buffer->device = device;
    device_child_add_ref(device);
    buffer->handle = allocate_handle();
    buffer->length = length;
    buffer->usage = usage;
    buffer->format = format;
    buffer->pool = pool;

    if (!emit_index_buffer_create(device, buffer)) {
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        HeapFree(GetProcessHeap(), 0, buffer);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    buffer->next_device_resource = device->index_buffers;
    device->index_buffers = buffer;
    *buffer_out = &buffer->iface;
    return D3D_OK;
}

static HRESULT WINAPI vb_lock(IDirect3DVertexBuffer8 *iface, UINT offset,
        UINT size, BYTE **data_out, DWORD flags)
{
    D8VertexBuffer *buffer = vb_from_iface(iface);
    if (!data_out || buffer->locked || offset > buffer->length)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    buffer->locked = TRUE;
    buffer->lock_offset = offset;
    buffer->lock_size = size;
    flags = d9wg_buffer_lock_flags(buffer->usage, buffer->pool, flags);
    buffer->lock_flags = flags;
    if (flags & D3DLOCK_DISCARD) {
        d9wg_clip_buffer_free(&buffer->clips, d9wg_vertex_windows_clip_release, NULL);
    }
    *data_out = buffer->shadow + offset;
    return D3D_OK;
}

static HRESULT WINAPI vb_unlock(IDirect3DVertexBuffer8 *iface)
{
    D8VertexBuffer *buffer = vb_from_iface(iface);
    BOOL result;
    if (!buffer->locked)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    result = (buffer->lock_flags & D3DLOCK_READONLY) ||
            emit_buffer_update(buffer->handle, buffer->lock_offset,
            buffer->shadow + buffer->lock_offset, buffer->lock_size,
            buffer->lock_flags);
    buffer->locked = FALSE;
    buffer->lock_offset = 0;
    buffer->lock_size = 0;
    buffer->lock_flags = 0;
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI vb_get_desc(IDirect3DVertexBuffer8 *iface,
        D3DVERTEXBUFFER_DESC *desc)
{
    D8VertexBuffer *buffer = vb_from_iface(iface);
    if (!desc)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = D3DFMT_VERTEXDATA;
    desc->Type = D3DRTYPE_VERTEXBUFFER;
    desc->Usage = buffer->usage;
    desc->Pool = buffer->pool;
    desc->Size = buffer->length;
    desc->FVF = buffer->fvf;
    return D3D_OK;
}

static HRESULT WINAPI ib_lock(IDirect3DIndexBuffer8 *iface, UINT offset,
        UINT size, BYTE **data_out, DWORD flags)
{
    D8IndexBuffer *buffer = ib_from_iface(iface);
    if (!data_out || buffer->locked || offset > buffer->length)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    buffer->locked = TRUE;
    buffer->lock_offset = offset;
    buffer->lock_size = size;
    flags = d9wg_buffer_lock_flags(buffer->usage, buffer->pool, flags);
    buffer->lock_flags = flags;
    *data_out = buffer->shadow + offset;
    return D3D_OK;
}

static HRESULT WINAPI ib_unlock(IDirect3DIndexBuffer8 *iface)
{
    D8IndexBuffer *buffer = ib_from_iface(iface);
    BOOL result;
    if (!buffer->locked)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    result = (buffer->lock_flags & D3DLOCK_READONLY) ||
            emit_buffer_update(buffer->handle, buffer->lock_offset,
            buffer->shadow + buffer->lock_offset, buffer->lock_size,
            buffer->lock_flags);
    buffer->locked = FALSE;
    buffer->lock_offset = 0;
    buffer->lock_size = 0;
    buffer->lock_flags = 0;
    return D8WG_TRACE_ERROR(result ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI ib_get_desc(IDirect3DIndexBuffer8 *iface,
        D3DINDEXBUFFER_DESC *desc)
{
    D8IndexBuffer *buffer = ib_from_iface(iface);
    if (!desc)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = buffer->format;
    desc->Type = D3DRTYPE_INDEXBUFFER;
    desc->Usage = buffer->usage;
    desc->Pool = buffer->pool;
    desc->Size = buffer->length;
    return D3D_OK;
}
