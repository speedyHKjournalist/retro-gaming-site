/* Actual refactor 2e3982d API8 emitters/setters, before shared state translation. */
static __inline unsigned d3d8_stage_state_to_sampler_state(unsigned state)
{
    switch (state) {
    case (13u): return (1u);
    case (14u): return (2u);
    case (25u): return (3u);
    case (15u): return (4u);
    case (16u): return (5u);
    case (17u): return (6u);
    case (18u): return (7u);
    case (19u): return (8u);
    case (20u): return (9u);
    case (21u): return (10u);
    default: return 0u;
    }
}

static BOOL emit_render_state(D8Device *device, D3DRENDERSTATETYPE state,
        DWORD value)
{
    D9WGSetRenderState command;

    command.device_handle = device->handle;
    command.reserved = 0;
    switch ((UINT)state) {
    case (10u):              /* no D3D9 equivalent */
    case (30u):                 /* never implemented by any driver */
    case (40u):            /* superseded by multisampling */
    case (153u): /* a device method in D3D9 */
    case (164u):            /* N-patch tessellation, see fill_caps */
    case (172u):
    case (173u):
        return TRUE;
    case (47u): {
        /*
         * D3D8: an integer 0..16 pulling the fragment towards the viewer.
         * D3D9: a float added to the depth value, so the same intent is a
         * negative bias. The host reads this state as raw float bits.
         */
        float bias = (float)(value > 16u ? 16u : value)
                * ((-0.000005f));
        command.state = (195u);
        CopyMemory(&command.value, &bias, sizeof(command.value));
        break;
    }
    default:
        command.state = (uint32_t)state;
        command.value = value;
        break;
    }
    return emit_command(D9WG_OP_SET_RENDER_STATE, &command, sizeof(command));
}

static BOOL emit_texture_stage_state(D8Device *device, DWORD stage,
        D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
    unsigned sampler_state = d3d8_stage_state_to_sampler_state((unsigned)state);

    if (sampler_state) {
        D9WGSetSamplerState command;
        command.device_handle = device->handle;
        command.sampler = stage;
        command.state = sampler_state;
        command.value = value;
        return emit_command(D9WG_OP_SET_SAMPLER_STATE, &command,
                sizeof(command));
    }
    {
        D9WGSetTextureStageState command;
        command.device_handle = device->handle;
        command.stage = stage;
        command.state = (uint32_t)state;
        command.value = value;
        return emit_command(D9WG_OP_SET_TEXTURE_STAGE_STATE, &command,
                sizeof(command));
    }
}

static HRESULT WINAPI device_set_render_state(IDirect3DDevice8 *iface,
        D3DRENDERSTATETYPE state, DWORD value)
{
    D8Device *device = device_from_iface(iface);
    if ((UINT)state >= D8WG_MAX_RENDER_STATES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block) {
        D8StateSnapshot *recorded = &device->recording_state_block->state;
        recorded->render_mask[state] = 1;recorded->render_states[state] = value;return D3D_OK;
    }
    if (device->render_states[state] == value)
        return D3D_OK;
    device->render_states[state] = value;
    return emit_render_state(device, state, value)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_render_state(IDirect3DDevice8 *iface,
        D3DRENDERSTATETYPE state, DWORD *value)
{
    if (!value || (UINT)state >= D8WG_MAX_RENDER_STATES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *value = device_from_iface(iface)->render_states[state];
    return D3D_OK;
}

static HRESULT WINAPI device_set_texture_stage_state(IDirect3DDevice8 *iface,
        DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
    D8Device *device = device_from_iface(iface);
    if (stage >= D8WG_MAX_TEXTURE_STAGES
            || (UINT)state >= D8WG_MAX_TEXTURE_STAGE_STATES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (device->recording_state_block) {
        D8StateSnapshot *recorded = &device->recording_state_block->state;
        recorded->texture_stage_mask[stage][state] = 1;recorded->texture_stage_states[stage][state] = value;return D3D_OK;
    }
    if (device->texture_stage_states[stage][state] == value)
        return D3D_OK;
    device->texture_stage_states[stage][state] = value;
    return emit_texture_stage_state(device, stage, state, value)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_texture_stage_state(IDirect3DDevice8 *iface,
        DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD *value)
{
    if (!value || stage >= D8WG_MAX_TEXTURE_STAGES
            || (UINT)state >= D8WG_MAX_TEXTURE_STAGE_STATES)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *value = device_from_iface(iface)->texture_stage_states[stage][state];
    return D3D_OK;
}
