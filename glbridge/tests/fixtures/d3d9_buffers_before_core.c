static HRESULT WINAPI device_create_vertex_buffer(IDirect3DDevice9 *iface,
        UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
        IDirect3DVertexBuffer9 **buffer_out, HANDLE *shared_handle)
{
    D9Device *device = device_from_iface(iface);
    D9VertexBuffer *buffer;
    (void)shared_handle;
    TRACE_MARK_ENTER("Device.CreateVertexBuffer");
    if (!buffer_out) {
        TRACE("FAIL CreateVertexBuffer length=%lu missing output -> %08lX",
                length, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *buffer_out = NULL;
    if (!length || pool > D3DPOOL_SCRATCH) {
        TRACE("FAIL CreateVertexBuffer length=%lu usage=%08lX fvf=%08lX pool=%lu -> %08lX",
                length, usage, fvf, (DWORD)pool, (DWORD)D3DERR_INVALIDCALL);
        HOSTLOG_FAILED("CreateVertexBuffer refused: length=%lu usage=%08lX "
                "fvf=%08lX pool=%lu", length, usage, fvf, (DWORD)pool);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    buffer = (D9VertexBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*buffer));
    if (!buffer) {
        TRACE("FAIL CreateVertexBuffer object allocation length=%lu -> %08lX",
                length, (DWORD)E_OUTOFMEMORY);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer", E_OUTOFMEMORY, NULL);
        TRACE_FLUSH();
        return E_OUTOFMEMORY;
    }
    buffer->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            length);
    if (!buffer->shadow) {
        HeapFree(GetProcessHeap(), 0, buffer);
        TRACE("FAIL CreateVertexBuffer shadow allocation length=%lu -> %08lX",
                length, (DWORD)E_OUTOFMEMORY);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer", E_OUTOFMEMORY, NULL);
        TRACE_FLUSH();
        return E_OUTOFMEMORY;
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
        TRACE("FAIL CreateVertexBuffer transport length=%lu handle=%08lX -> %08lX",
                length, buffer->handle, (DWORD)D3DERR_DRIVERINTERNALERROR);
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        HeapFree(GetProcessHeap(), 0, buffer);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer",
                D3DERR_DRIVERINTERNALERROR, NULL);
        TRACE_FLUSH();
        return D3DERR_DRIVERINTERNALERROR;
    }
    buffer->next_device_resource = device->vertex_buffers;
    device->vertex_buffers = buffer;
    *buffer_out = &buffer->iface;
    TRACE_BUFFER_CREATED(FALSE, length);
    TRACE_REGISTER_RANGE("VB_OBJECT", g_trace_vb_count, buffer, buffer,
            sizeof(*buffer));
    TRACE_REGISTER_RANGE("VB_SHADOW", g_trace_vb_count, buffer,
            buffer->shadow, buffer->length);
#ifdef D9WG_DIAGNOSTIC_TRACE
    if (g_trace_vb_count <= 4 || !(g_trace_vb_count & 127))
        TRACE("OK CreateVertexBuffer count=%ld bytes=%lu length=%lu usage=%08lX fvf=%08lX pool=%lu handle=%08lX object=%08lX shadow=%08lX shadow_end=%08lX",
                g_trace_vb_count, (DWORD)g_trace_vb_bytes, length, usage, fvf,
                (DWORD)pool, buffer->handle, (DWORD)(uintptr_t)*buffer_out,
                (DWORD)(uintptr_t)buffer->shadow,
                (DWORD)(uintptr_t)(buffer->shadow + buffer->length));
#endif
    TRACE_MARK_EXIT("Device.CreateVertexBuffer", D3D_OK, *buffer_out);
    return D3D_OK;
}

static HRESULT WINAPI device_create_index_buffer(IDirect3DDevice9 *iface,
        UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DIndexBuffer9 **buffer_out, HANDLE *shared_handle)
{
    D9Device *device = device_from_iface(iface);
    D9IndexBuffer *buffer;
    UINT index_size;
    (void)shared_handle;

    TRACE_MARK_ENTER("Device.CreateIndexBuffer");
    if (!buffer_out) {
        TRACE("FAIL CreateIndexBuffer length=%lu missing output -> %08lX",
                length, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *buffer_out = NULL;
    if (!length || pool > D3DPOOL_SCRATCH) {
        TRACE("FAIL CreateIndexBuffer length=%lu usage=%08lX format=%08lX pool=%lu -> %08lX",
                length, usage, (DWORD)format, (DWORD)pool,
                (DWORD)D3DERR_INVALIDCALL);
        HOSTLOG_FAILED("CreateIndexBuffer refused: length=%lu usage=%08lX "
                "format=%08lX pool=%lu", length, usage, (DWORD)format,
                (DWORD)pool);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (format == D3DFMT_INDEX16) index_size = 2;
    else if (format == D3DFMT_INDEX32) index_size = 4;
    else {
        TRACE("FAIL CreateIndexBuffer format=%08lX -> %08lX",
                (DWORD)format, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (length % index_size) {
        TRACE("FAIL CreateIndexBuffer unaligned length=%lu index_size=%lu -> %08lX",
                length, index_size, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }

    buffer = (D9IndexBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*buffer));
    if (!buffer) {
        TRACE("FAIL CreateIndexBuffer object allocation length=%lu -> %08lX",
                length, (DWORD)E_OUTOFMEMORY);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", E_OUTOFMEMORY, NULL);
        TRACE_FLUSH();
        return E_OUTOFMEMORY;
    }
    buffer->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            length);
    if (!buffer->shadow) {
        HeapFree(GetProcessHeap(), 0, buffer);
        TRACE("FAIL CreateIndexBuffer shadow allocation length=%lu -> %08lX",
                length, (DWORD)E_OUTOFMEMORY);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", E_OUTOFMEMORY, NULL);
        TRACE_FLUSH();
        return E_OUTOFMEMORY;
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
        TRACE("FAIL CreateIndexBuffer transport length=%lu handle=%08lX -> %08lX",
                length, buffer->handle, (DWORD)D3DERR_DRIVERINTERNALERROR);
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        HeapFree(GetProcessHeap(), 0, buffer);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer",
                D3DERR_DRIVERINTERNALERROR, NULL);
        TRACE_FLUSH();
        return D3DERR_DRIVERINTERNALERROR;
    }
    buffer->next_device_resource = device->index_buffers;
    device->index_buffers = buffer;
    *buffer_out = &buffer->iface;
    TRACE_BUFFER_CREATED(TRUE, length);
    TRACE_REGISTER_RANGE("IB_OBJECT", g_trace_ib_count, buffer, buffer,
            sizeof(*buffer));
    TRACE_REGISTER_RANGE("IB_SHADOW", g_trace_ib_count, buffer,
            buffer->shadow, buffer->length);
#ifdef D9WG_DIAGNOSTIC_TRACE
    if (g_trace_ib_count <= 4 || !(g_trace_ib_count & 127))
        TRACE("OK CreateIndexBuffer count=%ld bytes=%lu length=%lu usage=%08lX format=%08lX pool=%lu handle=%08lX object=%08lX shadow=%08lX shadow_end=%08lX",
                g_trace_ib_count, (DWORD)g_trace_ib_bytes, length, usage,
                (DWORD)format, (DWORD)pool, buffer->handle,
                (DWORD)(uintptr_t)*buffer_out,
                (DWORD)(uintptr_t)buffer->shadow,
                (DWORD)(uintptr_t)(buffer->shadow + buffer->length));
#endif
    TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3D_OK, *buffer_out);
    return D3D_OK;
}

static HRESULT WINAPI vb_lock(IDirect3DVertexBuffer9 *iface, UINT offset,
        UINT size, void **data_out, DWORD flags)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    if (!data_out || buffer->locked || offset > buffer->length)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
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

static HRESULT WINAPI vb_unlock(IDirect3DVertexBuffer9 *iface)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    BOOL result;
    if (!buffer->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    result = (buffer->lock_flags & D3DLOCK_READONLY)
            || emit_buffer_update(buffer->handle, buffer->lock_offset,
                    buffer->shadow + buffer->lock_offset, buffer->lock_size,
                    buffer->lock_flags);
    buffer->locked = FALSE;
    buffer->lock_offset = 0;
    buffer->lock_size = 0;
    buffer->lock_flags = 0;
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI vb_get_desc(IDirect3DVertexBuffer9 *iface,
        D3DVERTEXBUFFER_DESC *desc)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    if (!desc)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = D3DFMT_VERTEXDATA;
    desc->Type = D3DRTYPE_VERTEXBUFFER;
    desc->Usage = buffer->usage;
    desc->Pool = buffer->pool;
    desc->Size = buffer->length;
    desc->FVF = buffer->fvf;
    return D3D_OK;
}

static HRESULT WINAPI ib_lock(IDirect3DIndexBuffer9 *iface, UINT offset,
        UINT size, void **data_out, DWORD flags)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    if (!data_out || buffer->locked || offset > buffer->length)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    buffer->locked = TRUE;
    buffer->lock_offset = offset;
    buffer->lock_size = size;
    flags = d9wg_buffer_lock_flags(buffer->usage, buffer->pool, flags);
    buffer->lock_flags = flags;
    *data_out = buffer->shadow + offset;
    return D3D_OK;
}

static HRESULT WINAPI ib_unlock(IDirect3DIndexBuffer9 *iface)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    BOOL result;
    if (!buffer->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    result = (buffer->lock_flags & D3DLOCK_READONLY)
            || emit_buffer_update(buffer->handle, buffer->lock_offset,
                    buffer->shadow + buffer->lock_offset, buffer->lock_size,
                    buffer->lock_flags);
    buffer->locked = FALSE;
    buffer->lock_offset = 0;
    buffer->lock_size = 0;
    buffer->lock_flags = 0;
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI ib_get_desc(IDirect3DIndexBuffer9 *iface,
        D3DINDEXBUFFER_DESC *desc)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    if (!desc)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = buffer->format;
    desc->Type = D3DRTYPE_INDEXBUFFER;
    desc->Usage = buffer->usage;
    desc->Pool = buffer->pool;
    desc->Size = buffer->length;
    return D3D_OK;
}
