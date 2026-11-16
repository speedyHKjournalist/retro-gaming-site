static HRESULT WINAPI device_process_vertices(IDirect3DDevice9 *iface,
        UINT src_start, UINT dst_index, UINT count,
        IDirect3DVertexBuffer9 *dst, IDirect3DVertexDeclaration9 *decl,
        DWORD flags)
{
    D9Device *device = device_from_iface(iface);
    D9VertexBuffer *destination;
    D9ProcessLayout destination_layout;
    D3DMATRIX world_view;
    D3DMATRIX world_view_projection;
    BOOL lighting;
    BOOL copy_data;
    uint32_t validated_offset;

    TRACE_MARK_ENTER("Device.ProcessVertices");
    if (!dst || dst->lpVtbl != &g_vb_vtbl) {
        TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    destination = vb_from_iface(dst);
    if (destination->device != device || destination->locked || !count) {
        TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (device->vertex_shader) {
        HOSTLOG_REFUSED("ProcessVertices refused: a vertex shader is bound, "
                "and running the fixed-function pipeline in its place would "
                "silently produce different geometry");
        TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    /* The output layout is the declaration D3D9 passes, or the destination
     * buffer's own FVF when it passes none. */
    if (decl) {
        D9VertexDeclaration *output = decl_from_iface(decl);
        D9WGVertexElement elements[D3DMAXDECLLENGTH];
        UINT index;
        if (output->device != device
                || output->element_count > D3DMAXDECLLENGTH) {
            TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
        for (index = 0; index < output->element_count; ++index) {
            elements[index].stream = (uint16_t)output->elements[index].Stream;
            elements[index].offset = (uint16_t)output->elements[index].Offset;
            elements[index].type = (uint8_t)output->elements[index].Type;
            elements[index].method = (uint8_t)output->elements[index].Method;
            elements[index].usage = (uint8_t)output->elements[index].Usage;
            elements[index].usage_index =
                    (uint8_t)output->elements[index].UsageIndex;
        }
        if (!d9_build_process_layout(elements, output->element_count,
                &destination_layout)) {
            HOSTLOG_REFUSED("ProcessVertices refused: the output declaration "
                    "uses an element type this software vertex path cannot "
                    "write");
            TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
    } else {
        D9WGVertexElement elements[D3DMAXDECLLENGTH];
        UINT element_count = 0;
        if (!destination->fvf
                || !fvf_to_declaration(destination->fvf, elements,
                        &element_count)
                || !d9_build_process_layout(elements, element_count,
                        &destination_layout)) {
            TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
    }
    /* D3D9 refuses to transform geometry that already is, and requires the
     * result to be pre-transformed. */
    if (!destination_layout.position_transformed ||
            !d9wg_process_canonical_output(&destination_layout) ||
            !d9wg_process_range(destination->length, 0, destination_layout.stride,
                destination_layout.stride, dst_index, count, &validated_offset))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    d9_multiply_matrix((const D3DMATRIX *)device->transforms[D3DTS_WORLD],
            (const D3DMATRIX *)device->transforms[D3DTS_VIEW], &world_view);
    d9_multiply_matrix(&world_view,
            (const D3DMATRIX *)device->transforms[D3DTS_PROJECTION],
            &world_view_projection);
    D9WGTNLLighting light_state;
    if (!d9_prepare_lighting(device, &world_view, &light_state))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    D9WGVertexState vertex_state;
    d9wg_vertex_state_init(&vertex_state, device->render_states);
    if (!d9wg_vertex_textures_init(&vertex_state, device->texture_stage_states,
            sizeof(device->texture_stage_states[0]), device->transforms[D3DTS_TEXTURE0]))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    d9wg_vertex_clip_planes_init(&vertex_state, device->render_states[D3DRS_CLIPPLANEENABLE],
            device->clip_planes, device->transforms[D3DTS_WORLD]);
    lighting = light_state.flags.lighting != 0;
    (void)lighting;
    /* Transformed channels are always written; this flag only suppresses
     * passthrough data. */
    copy_data = (flags & D3DPV_DONOTCOPYDATA) == 0;

    {
        D9WGProcessViewport viewport = { (float)device->viewport.X, (float)device->viewport.Y,
                (float)device->viewport.Width, (float)device->viewport.Height,
                device->viewport.MinZ, device->viewport.MaxZ };
        D9ProcessInput *input;
        HRESULT input_error = d9_process_input(device, destination, src_start, count, FALSE, &input);
        if (FAILED(input_error)) return TRACE_REFUSE(input_error);
        uint32_t blend_error = d9wg_vertex_windows_blend(&vertex_state, device->render_states,
                device->transforms[D3DTS_WORLD], 256, 0, device->transforms[D3DTS_VIEW], device->transforms[D3DTS_PROJECTION]);
        if (blend_error) {
            d9_process_input_free(input);
            return TRACE_REFUSE(blend_error == 2 ? E_OUTOFMEMORY : D3DERR_INVALIDCALL);
        }
        D3DCAPS9 caps;fill_caps(&caps);
        float guard[4] = { caps.GuardBandLeft, caps.GuardBandTop, caps.GuardBandRight, caps.GuardBandBottom };
        D9WGClipBatch *clip_batch;
        uint32_t clip_error = d9wg_vertex_windows_clip(&vertex_state, device->render_states[D3DRS_CLIPPING],
                validated_offset + destination_layout.position, destination_layout.stride, count, &viewport, guard, &clip_batch);
        if (clip_error) {
            d9wg_vertex_windows_blend_free(&vertex_state);
            d9_process_input_free(input);
            return TRACE_REFUSE(clip_error == 2 ? E_OUTOFMEMORY : D3DERR_INVALIDCALL);
        }
        BOOL processed;
        if (input->gather) processed = d9wg_process_vertices_streams(&input->layout,
                input->streams, src_start, &destination_layout, destination->shadow,
                destination->length, dst_index, count, &world_view, &world_view_projection,
                &viewport, &light_state, copy_data, &vertex_state);
        else processed = d9wg_process_vertices(&input->fast_layout,
                input->streams[0].vertices, input->streams[0].length,
                input->streams[0].base, input->streams[0].stride, src_start,
                &destination_layout, destination->shadow, destination->length, dst_index, count,
                &world_view, &world_view_projection, &viewport, &light_state, copy_data, &vertex_state);
        d9wg_vertex_windows_blend_free(&vertex_state);
        d9_process_input_free(input);
        if (!processed) {
            if (clip_batch) HeapFree(GetProcessHeap(), 0, clip_batch);
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
        d9wg_clip_status_processed(&device->clip_status, clip_batch);
        if (!clip_batch) device->clip_status_valid = TRUE;
        d9wg_clip_buffer_commit(&destination->clips, clip_batch, validated_offset,
                count * destination_layout.stride, d9wg_vertex_windows_clip_release, NULL);
    }

    /* The transformed vertices live in the destination's guest shadow; the
     * host needs them too, since the app will draw straight from the buffer. */
    TRACE("OK ProcessVertices device=%08lX count=%lu stride=%lu lit=%lu",
            device->handle, count, destination_layout.stride,
            (DWORD)lighting);
    TRACE_MARK_EXIT("Device.ProcessVertices", D3D_OK, NULL);
    return emit_buffer_update(destination->handle,
            dst_index * destination_layout.stride,
            destination->shadow + dst_index * destination_layout.stride,
            count * destination_layout.stride, 0)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}
