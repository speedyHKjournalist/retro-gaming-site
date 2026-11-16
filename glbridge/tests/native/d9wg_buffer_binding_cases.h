static D9Texture *new_buffer(D9Device *device, UINT pool, BOOL index)
{
    D9Texture *buffer = HeapAlloc(NULL, 8, sizeof(*buffer));
    buffer->iface.lpVtbl = index ? &g_ib_vtbl : &g_vb_vtbl;
    buffer->refcount = 1;buffer->device = device;buffer->handle = allocate_handle();
    buffer->length = index ? 12 : 64;buffer->pool = pool;
    buffer->shadow_allocation = buffer->shadow = HeapAlloc(NULL, 8, buffer->length);
    if (!index) buffer->clips = HeapAlloc(NULL, 8, 16);
    D9Texture **list = index ? &device->index_buffers : &device->vertex_buffers;
    buffer->next_device_resource = *list;*list = buffer;device_child_add_ref(device);
    return buffer;
}
static int buffer_binding_cases(void)
{
    const UINT streams[] = {0, 1, 15};
    UINT profiles = 0, revivals = 0, old_checks = checks;
    for (UINT round = 0; round < 64; round++) for (UINT pool = 0; pool < 4; pool++) for (UINT order = 0; order < 4; order++) {
        Parent parent = {0};D9Device *device = new_device(&parent);
        D9VertexBuffer *vb = new_buffer(device, pool, FALSE);
        D9IndexBuffer *ib = new_buffer(device, pool, TRUE);
        for (UINT i = 0; i < 3; i++) CHECK(!device_set_stream_source(&device->iface, streams[i], &vb->iface, 4, 12));
        CHECK(!device_set_indices(&device->iface, &ib->iface));
        CHECK(vb->refcount == 1 && vb->binding_refs == 3 && ib->refcount == 1 && ib->binding_refs == 1 && device->refcount == 3 && device->child_parent_refs == 2);
        CHECK(!vb_release(&vb->iface) && !ib_release(&ib->iface));
        CHECK(device->refcount == 1 && !device->child_parent_refs && !device_has_reset_blockers(device));
        CHECK(!vb_release(&vb->iface) && !ib_release(&ib->iface));
        for (UINT cycle = 0; cycle < 8; cycle++) {
            Interface *vertex = NULL, *indices = NULL;UINT offset = 0, stride = 0;
            CHECK(!device_get_stream_source(&device->iface, streams[cycle % 3], &vertex, &offset, &stride) && vertex == &vb->iface && offset == 4 && stride == 12);
            CHECK(vb->refcount == 1 && device->refcount == 2 && device_has_reset_blockers(device) == (pool == 0));
            CHECK(!device_get_indices(&device->iface, &indices) && indices == &ib->iface && ib->refcount == 1 && device->refcount == 3 && device->child_parent_refs == 2);
            CHECK(vb_add_ref(vertex) == 2 && ib_add_ref(indices) == 2 && device->refcount == 3);
            CHECK(vb_release(vertex) == 1 && ib_release(indices) == 1);
            CHECK(!device_set_stream_source(&device->iface, streams[cycle % 3], vertex, 4, 12) && !device_set_indices(&device->iface, indices));
            CHECK(vb->binding_refs == 3 && ib->binding_refs == 1);
            CHECK(!vb_release(vertex) && device->refcount == 2 && device_has_reset_blockers(device) == (pool == 0));
            CHECK(!ib_release(indices) && device->refcount == 1 && !device_has_reset_blockers(device));
            revivals += 2;
        }
        vb->locked = TRUE;CHECK(device_has_reset_blockers(device) == (pool == 0));vb->locked = FALSE;ib->locked = TRUE;CHECK(device_has_reset_blockers(device) == (pool == 0));ib->locked = FALSE;
        LONG vertex_holds = vb->binding_refs, index_holds = ib->binding_refs;
        CHECK(device_set_stream_source(&device->iface, 16, &vb->iface, 4, 12) == -1 && device_set_stream_source(&device->iface, 0, &vb->iface, vb->length, 12) == -1);
        CHECK(device_get_stream_source(&device->iface, 0, NULL, NULL, NULL) == -1 && device_get_indices(&device->iface, NULL) == -1);
        D9Device foreign = {0};D9VertexBuffer other_vb = {.iface = {&g_vb_vtbl}, .device = &foreign};D9IndexBuffer other_ib = {.iface = {&g_ib_vtbl}, .device = &foreign};
        CHECK(device_set_stream_source(&device->iface, 0, &other_vb.iface, 4, 12) == -1 && device_set_indices(&device->iface, &other_ib.iface) == -1);
        CHECK(vb->binding_refs == vertex_holds && ib->binding_refs == index_holds);
        UINT old_destroys = destroys;
        if (order == 0) {
            for (UINT i = 0; i < 3; i++) CHECK(!device_set_stream_source(&device->iface, streams[i], NULL, 0, 0));
            CHECK(destroys == old_destroys + 1 && !device->vertex_buffers);
            CHECK(!device_set_indices(&device->iface, NULL) && destroys == old_destroys + 2 && !device->index_buffers && live == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        } else if (order == 1) {
            CHECK(!device_release(&device->iface) && !live && !parent.refs && destroys == old_destroys + 2);
        } else if (order == 2) {
            Interface *vertex = NULL, *indices = NULL;UINT stride;
            CHECK(!device_get_stream_source(&device->iface, 0, &vertex, NULL, &stride) && !device_get_indices(&device->iface, &indices));
            device_clear_bindings(device);CHECK(vb->refcount == 1 && !vb->binding_refs && ib->refcount == 1 && !ib->binding_refs && device->refcount == 3 && destroys == old_destroys);
            CHECK(device_release(&device->iface) == 2 && !vb_release(vertex) && !ib_release(indices) && !live && !parent.refs && destroys == old_destroys + 2);
        } else {
            clear_stream_zero_after_up(device);CHECK(!device->streams[0].buffer && !device->streams[0].stride && vb->binding_refs == 2 && !vb->refcount);
            clear_indices_after_indexed_up(device);CHECK(!device->index_buffer && !device->index_buffers && destroys == old_destroys + 1);
            CHECK(!device_set_stream_source(&device->iface, 1, NULL, 0, 0) && !device_set_stream_source(&device->iface, 15, NULL, 0, 0));
            CHECK(destroys == old_destroys + 2 && !device->vertex_buffers && live == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        }
        profiles++;
    }
    printf("PASS actual D3D9 buffer bindings: %u checks, %u pool/order profiles, %u public-zero revivals; three VB stream slots and one IB slot, exact public/device refs, getter offsets, DEFAULT locks, UP cleanup, shadow/clip teardown and complete final-owner cleanup\n", checks - old_checks, profiles, revivals);
    return 0;
}
