/* Actual prior9 initializer from site2d3188c;positive ordinary9 oracle. */
static void device_init_states(D9Device *device)
{
    UINT slot;
    UINT stage;
    device->current_palette = 0;

    /* Setters suppress equal values, so the shadow and host must start from
     * the same generated defaults, including creation-time depth state. */
    d9wg_clip_status_init(&device->clip_status);device->clip_status_valid = TRUE;
    ZeroMemory(device->render_states, sizeof(device->render_states));
    ZeroMemory(device->texture_stage_states,
            sizeof(device->texture_stage_states));
    ZeroMemory(device->sampler_states, sizeof(device->sampler_states));

    D3DCAPS9 caps;fill_caps(&caps);
    DWORD point_max_bits;CopyMemory(&point_max_bits, &caps.MaxPointSize, sizeof(point_max_bits));
    for (UINT state = 0; state < sizeof(device->render_states)/sizeof(device->render_states[0]); ++state)
        device->render_states[state] = d9wg_render_default(9, state,
                device->present.EnableAutoDepthStencil, point_max_bits);
    d9wg_object_defaults(9, &device->material, device->clip_planes, D9_MAX_CLIP_PLANES);
    d9wg_light_free(&device->extra_lights);
    ZeroMemory(device->lights, sizeof(device->lights));
    ZeroMemory(device->light_set, sizeof(device->light_set));
    ZeroMemory(device->light_enabled, sizeof(device->light_enabled));
    for (stage = 0; stage < D9_MAX_TEXTURE_STAGES; ++stage)
        for (UINT state = 0; state < sizeof(device->texture_stage_states[stage])/sizeof(device->texture_stage_states[stage][0]); ++state)
            device->texture_stage_states[stage][state] = d9wg_texture_default(9, stage, state);
    for (slot = 0; slot < D9_MAX_SAMPLERS; ++slot)
        for (UINT state = 0; state < sizeof(device->sampler_states[slot])/sizeof(device->sampler_states[slot][0]); ++state)
            device->sampler_states[slot][state] = d9wg_sampler_default(state);
    for (slot = 0; slot < D9_MAX_STREAMS; ++slot)
        device->streams[slot].frequency = 1u;
    for (slot = 0; slot < D9_MAX_TRANSFORMS; ++slot) {
        ZeroMemory(device->transforms[slot], sizeof(device->transforms[slot]));
        device->transforms[slot][0] = 1.0f;
        device->transforms[slot][5] = 1.0f;
        device->transforms[slot][10] = 1.0f;
        device->transforms[slot][15] = 1.0f;
    }
}
