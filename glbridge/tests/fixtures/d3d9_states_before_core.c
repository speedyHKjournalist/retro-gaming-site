/* Actual refactor 2e3982d ordinary9 setters, before private8 state entry points. */
static HRESULT WINAPI device_set_render_state(IDirect3DDevice9 *iface,
        D3DRENDERSTATETYPE state, DWORD value)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetRenderState command;
    if ((UINT)state >= D9_MAX_RENDER_STATES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_render_state(device, (UINT)state, value)) return D3D_OK;
    if (device->render_states[state] == value)
        return D3D_OK;
    /*
     * Adaptive tessellation is the one part of the higher-order primitive
     * family with nothing behind it. It is not a refinement of the N-patch
     * level that could be approximated: D3D9 ties it to displacement mapping,
     * where the per-edge level comes from sampling D3DDMAPSAMPLER's texture
     * *during* tessellation, and nothing in a stage-less pipeline can sample a
     * texture there. Setting it therefore changes nothing, and a title that
     * enables it and sees no extra detail deserves to be told why rather than
     * left to wonder whether its meshes are wrong.
     */
    if (state == D3DRS_ENABLEADAPTIVETESSELLATION && value)
        HOSTLOG_REFUSED("D3DRS_ENABLEADAPTIVETESSELLATION is not implemented: "
                "its per-edge level comes from sampling D3DDMAPSAMPLER during "
                "tessellation, which has no equivalent here. N-patches still "
                "tessellate at the SetNPatchMode level");
    /*
     * The vendor render-state hacks share the D3DRS_ADAPTIVETESS_* slots,
     * because D3D9 never grew states of their own for them. ADAPTIVETESS_Y
     * carrying 'ATOC' is alpha-to-coverage, which the host implements natively;
     * ADAPTIVETESS_X carrying 'NVDB' is the depth bounds test, which nothing
     * can. Naming the second one matters because it fails *permissively*: an
     * app that enables depth bounds and is ignored draws pixels it meant to
     * reject, which reads as a depth bug rather than a missing feature.
     */
    if (state == D3DRS_ADAPTIVETESS_X && value == D9FOURCC_NVDB)
        HOSTLOG_REFUSED("the NVDB depth bounds test is not implemented: "
                "WebGPU has no depth bounds state, and ignoring it draws "
                "pixels the app meant to reject rather than fewer");
    /*
     * RESZ: the app writes a magic value into D3DRS_POINTSIZE to ask the driver
     * to resolve the bound multisampled depth buffer into an INTZ texture. It
     * cannot be served here, and the reason is a hard one rather than a missing
     * feature: the host's depth attachment is depth24plus-stencil8, and WebGPU
     * gives that format no copy-source capability at all, so there is no
     * operation -- copy, blit or resolve -- that can read it out. WebGPU has no
     * multisampled depth resolve either.
     *
     * What the trick exists to work around is binding the depth buffer as a
     * texture while it is also the depth attachment. That specific need is
     * already met: an INTZ texture can be bound to a sampler and read directly
     * (see depthSampleModeFor in the executor), which is the path a title
     * should take here.
     *
     * The magic value is a float NaN pattern, so no legitimate point size can
     * collide with it.
     */
    if (state == D3DRS_POINTSIZE && value == 0x7FA05000u)
        HOSTLOG_REFUSED("the RESZ depth-resolve trick is not implemented: the "
                "host's depth attachment is depth24plus-stencil8, which WebGPU "
                "gives no copy-source capability, so nothing can read it out. "
                "Bind an INTZ texture and sample it directly instead");
    device->render_states[state] = value;
    command.device_handle = device->handle;
    command.state = state;
    command.value = value;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_RENDER_STATE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_set_texture_stage_state(IDirect3DDevice9 *iface,
        DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetTextureStageState command;
    if (stage >= D9_MAX_TEXTURE_STAGES
            || (UINT)state >= D9_MAX_TEXTURE_STAGE_STATES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_texture_stage_state(device, stage, (UINT)state, value)) return D3D_OK;
    if (device->texture_stage_states[stage][state] == value)
        return D3D_OK;
    device->texture_stage_states[stage][state] = value;
    command.device_handle = device->handle;
    command.stage = stage;
    command.state = state;
    command.value = value;
    return emit_command(D9WG_OP_SET_TEXTURE_STAGE_STATE, &command,
            sizeof(command)) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_set_sampler_state(IDirect3DDevice9 *iface,
        DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetSamplerState command;
    UINT slot;
    if (!sampler_state_slot(sampler, &slot)
            || (UINT)type >= D9_MAX_SAMPLER_STATES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_sampler_state(device, slot, (UINT)type, value)) return D3D_OK;
    if (device->sampler_states[slot][type] == value)
        return D3D_OK;
    device->sampler_states[slot][type] = value;
    command.device_handle = device->handle;
    command.sampler = sampler;
    command.state = type;
    command.value = value;
    return emit_command(D9WG_OP_SET_SAMPLER_STATE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}
