/* Actual legacy8 initializer,site2d3188c. */
static void device_init_states(D8Device *device)
{
    UINT stage;
    UINT transform;
    device->current_palette = 0;
    d9wg_clip_status_init(&device->clip_status);device->clip_status_valid = TRUE;
    ZeroMemory(device->render_states, sizeof(device->render_states));
    ZeroMemory(device->texture_stage_states,
            sizeof(device->texture_stage_states));
    D3DCAPS8 caps;fill_caps(&caps);
    DWORD point_max_bits;CopyMemory(&point_max_bits, &caps.MaxPointSize, sizeof(point_max_bits));
    for (UINT state = 0; state < sizeof(device->render_states)/sizeof(device->render_states[0]); ++state)
        device->render_states[state] = d9wg_render_default(8, state,
                device->present.EnableAutoDepthStencil, point_max_bits);
    d9wg_object_defaults(8, &device->material, device->clip_planes, D8WG_MAX_CLIP_PLANES);
    free_extra_lights(&device->extra_lights);
    ZeroMemory(device->lights, sizeof(device->lights));
    ZeroMemory(device->light_set, sizeof(device->light_set));
    ZeroMemory(device->light_enabled, sizeof(device->light_enabled));
    for (transform = 0; transform < D8WG_MAX_TRANSFORMS; ++transform) {
        ZeroMemory(&device->transforms[transform], sizeof(D3DMATRIX));
        device->transforms[transform].m[0][0] = 1.0f;
        device->transforms[transform].m[1][1] = 1.0f;
        device->transforms[transform].m[2][2] = 1.0f;
        device->transforms[transform].m[3][3] = 1.0f;
    }
    for (stage = 0; stage < D8WG_MAX_TEXTURE_STAGES; ++stage)
        for (UINT state = 0; state < sizeof(device->texture_stage_states[stage])/sizeof(device->texture_stage_states[stage][0]); ++state)
            device->texture_stage_states[stage][state] = d9wg_texture_default(8, stage, state);

}
