static int stream_metadata(D9Device *device, UINT slot, UINT wanted_offset, UINT wanted_stride)
{
    Interface *out = (void*)1;UINT offset = 0xffffffff,stride = 0xffffffff;
    CHECK(!device_get_stream_source(&device->iface,slot,&out,&offset,&stride) && !out && offset == wanted_offset && stride == wanted_stride);
    return 0;
}
static int stream_binding_cases(void)
{
    UINT old_checks = checks, profiles = 0, nulls = 0, captures = 0;
    const UINT offsets[] = {0,4,16,60}, strides[] = {0,12,20,64};
    for (UINT round = 0; round < 8; round++) for (UINT pool = 0; pool < 4; pool++) for (UINT slot = 0; slot < 16; slot++) {
        Parent parent = {0};D9Device *device = new_device(&parent);UINT old_destroys = destroys;
        for (UINT cycle = 0; cycle < 8; cycle++) {
            D9VertexBuffer *buffer = new_buffer(device,pool,FALSE);UINT offset = offsets[cycle % 4],stride = strides[(cycle + round) % 4];
            CHECK(!device_set_stream_source(&device->iface,slot,&buffer->iface,offset,stride) && !vb_release(&buffer->iface));
            UINT emitted = stream_emissions;
            CHECK(!device_set_stream_source(&device->iface,slot,NULL,0xfffffffc,0xffffffff) && stream_emissions == emitted + 1);
            CHECK(!stream_metadata(device,slot,offset,stride) && !device->vertex_buffers && device->refcount == 1 && !device->child_parent_refs);
            CHECK(!device_set_stream_source(&device->iface,slot,NULL,0,0) && stream_emissions == emitted + 1);
            CHECK(!stream_metadata(device,slot,offset,stride));
            nulls++;
        }
        CHECK(destroys == old_destroys + 8);
        D9VertexBuffer *live_buffer = new_buffer(device,pool,FALSE), *recorded = new_buffer(device,pool,FALSE);
        CHECK(!device_set_stream_source(&device->iface,slot,&live_buffer->iface,4,12));
        CHECK(!device_begin_state_block(&device->iface));
        CHECK(!device_set_stream_source(&device->iface,slot,&recorded->iface,16,20));
        UINT emitted = stream_emissions;
        CHECK(!device_set_stream_source(&device->iface,slot,NULL,0xfffffffc,0xffffffff) && stream_emissions == emitted);
        D9StateBlock *block = device->recording_result;
        CHECK(block->has_stream[slot] && !block->streams[slot].buffer && block->streams[slot].offset == 4 && block->streams[slot].stride == 12 && device->streams[slot].buffer == live_buffer && device->streams[slot].offset == 4 && device->streams[slot].stride == 12 && !recorded->binding_refs);
        CHECK(!vb_release(&recorded->iface));
        Interface *snapshot = NULL;CHECK(!device_end_state_block(&device->iface,&snapshot) && snapshot == &block->iface);
        CHECK(!vb_release(&live_buffer->iface) && !device_set_stream_source(&device->iface,slot,&live_buffer->iface,8,24));
        CHECK(!state_block_apply_method(snapshot) && !stream_metadata(device,slot,4,12) && !device->vertex_buffers);
        CHECK(!state_block_capture_method(snapshot));
        D9VertexBuffer *replacement = new_buffer(device,pool,FALSE);
        CHECK(!device_set_stream_source(&device->iface,slot,&replacement->iface,16,20) && !vb_release(&replacement->iface));
        CHECK(!state_block_apply_method(snapshot) && !stream_metadata(device,slot,4,12) && !device->vertex_buffers);
        CHECK(!state_block_release(snapshot) && device->refcount == 1 && !device->child_parent_refs);
        captures++;
        D9VertexBuffer *up_buffer = new_buffer(device,pool,FALSE);
        CHECK(!device_set_stream_source(&device->iface,0,&up_buffer->iface,16,20) && !vb_release(&up_buffer->iface));
        clear_stream_zero_after_up(device);CHECK(!stream_metadata(device,0,0,0) && !device->vertex_buffers);
        CHECK(!device_set_stream_binding(device,0,NULL,32,64));clear_stream_zero_after_up(device);CHECK(!stream_metadata(device,0,0,0));
        Interface *guard = (void*)1;UINT offset = 0xdead,stride = 0xbeef;
        CHECK(device_set_stream_source(&device->iface,16,NULL,0,0) == -1 && device_get_stream_source(&device->iface,16,&guard,&offset,&stride) == -1 && guard == (void*)1 && offset == 0xdead && stride == 0xbeef);
        CHECK(!device_release(&device->iface) && !live && !parent.refs);
        profiles++;
    }
    printf("PASS actual D3D9 stream metadata: %u checks, %u pool/slot profiles, %u NULL preserves and %u recorded NULL/capture/apply sequences; ignored incoming metadata, live recording source, exact internal restoration, duplicate filtering, successful-UP zeroing and private storage cleanup\n",checks-old_checks,profiles,nulls,captures);
    return 0;
}
