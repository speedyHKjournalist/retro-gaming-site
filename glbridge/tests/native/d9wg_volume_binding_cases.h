static D9VolumeTexture *new_volume(D9Device *device, UINT pool)
{
    D9VolumeTexture *texture = HeapAlloc(NULL, 8, sizeof(*texture));
    texture->iface.lpVtbl = &g_volume_texture_vtbl;texture->refcount = 1;
    texture->device = device;texture->handle = allocate_handle();texture->pool = pool;
    texture->level_count = 2;texture->levels = HeapAlloc(NULL, 8, 2 * sizeof(*texture->levels));
    for (UINT i = 0; i < 2; i++) {
        texture->levels[i].shadow = HeapAlloc(NULL, 8, 64 >> i);
        D9Volume *volume = HeapAlloc(NULL, 8, sizeof(*volume));
        volume->texture = texture;volume->level = i;
        texture->levels[i].level_volume = volume;
    }
    texture->next_device_resource = device->volume_textures;
    device->volume_textures = texture;device_child_add_ref(device);
    return texture;
}
static int volume_binding_cases(void)
{
    const UINT stages[] = {0, 15, 257, 260};
    UINT profiles = 0, revivals = 0, old_checks = checks;
    for (UINT round = 0; round < 64; round++) for (UINT pool = 0; pool < 4; pool++) for (UINT order = 0; order < 4; order++) {
        Parent parent = {0};D9Device *device = new_device(&parent);
        D9VolumeTexture *texture = new_volume(device, pool);Interface *views[2];
        for (UINT i = 0; i < 4; i++) CHECK(!device_set_texture(&device->iface, stages[i], &texture->iface));
        CHECK(texture->refcount == 1 && texture->binding_refs == 4 && device->refcount == 2 && device->child_parent_refs == 1);
        for (UINT i = 0; i < 2; i++) {CHECK(!volume_texture_get_volume_level(&texture->iface, i, &views[i]));CHECK(((D9Volume *)views[i])->texture == texture && ((D9Volume *)views[i])->level == i);}
        CHECK(views[0] != views[1] && texture->refcount == 3 && device->refcount == 2);
        CHECK(volume_release(views[0]) == 2 && volume_release(views[1]) == 1 && !volume_texture_release(&texture->iface));
        CHECK(!texture->refcount && texture->binding_refs == 4 && device->refcount == 1 && !device->child_parent_refs && !device_has_reset_blockers(device));
        CHECK(!volume_texture_release(&texture->iface));
        for (UINT cycle = 0; cycle < 8; cycle++) {
            Interface *queried = NULL, *view = NULL;
            CHECK(!device_get_texture(&device->iface, stages[cycle % 4], &queried) && queried == &texture->iface);
            CHECK(texture->refcount == 1 && device->refcount == 2 && device->child_parent_refs == 1 && device_has_reset_blockers(device) == (pool == 0));
            CHECK(!volume_texture_get_volume_level(&texture->iface, cycle % 2, &view) && view == views[cycle % 2] && texture->refcount == 2 && device->refcount == 2);
            CHECK(volume_texture_add_ref(queried) == 3 && volume_release(view) == 2 && volume_texture_release(queried) == 1);
            CHECK(!volume_texture_release(queried) && device->refcount == 1 && !device_has_reset_blockers(device));
            CHECK(!device_set_texture(&device->iface, stages[cycle % 4], queried) && texture->binding_refs == 4);
            revivals++;
        }
        texture->levels[1].locked = TRUE;CHECK(device_has_reset_blockers(device) == (pool == 0));texture->levels[1].locked = FALSE;
        Interface *sentinel = (void *)1;LONG refs = texture->binding_refs;
        CHECK(volume_texture_get_volume_level(&texture->iface, 2, &sentinel) == -1 && sentinel == (void *)1);
        CHECK(volume_texture_get_volume_level(&texture->iface, 0, NULL) == -1 && device_get_texture(&device->iface, 256, &sentinel) == -1);
        D9Device foreign = {0};D9VolumeTexture other = {.iface = {&g_volume_texture_vtbl}, .device = &foreign};
        CHECK(device_set_texture(&device->iface, 0, &other.iface) == -1 && texture->binding_refs == refs && device->volume_bindings[0] == texture);
        UINT old_destroys = destroys;
        if (order == 0) {
            for (UINT i = 0; i < 4; i++) CHECK(!device_set_texture(&device->iface, stages[i], NULL));
            CHECK(destroys == old_destroys + 1 && !device->volume_textures && live == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        } else if (order == 1) {
            CHECK(!device_release(&device->iface) && !live && !parent.refs && destroys == old_destroys + 1);
        } else if (order == 2) {
            Interface *queried = NULL;CHECK(!device_get_texture(&device->iface, 0, &queried));
            device_clear_bindings(device);CHECK(texture->refcount == 1 && !texture->binding_refs && device->refcount == 2 && destroys == old_destroys);
            CHECK(device_release(&device->iface) == 1);CHECK(!volume_texture_release(queried) && !live && !parent.refs && destroys == old_destroys + 1);
        } else {
            device_clear_bindings(device);CHECK(device->refcount == 1 && !device->volume_textures && destroys == old_destroys + 1 && live == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        }
        profiles++;
    }
    for (UINT round = 0; round < 64; round++) {
        Parent parent = {0};D9Device *device = new_device(&parent);
        D9Texture *plain = new_texture(device, 0);CHECK(!device_set_texture(&device->iface, 0, &plain->iface));CHECK(!texture_release(&plain->iface));
        D9CubeTexture *cube = new_cube(device, 0);CHECK(!device_set_texture(&device->iface, 0, &cube->iface));CHECK(!device->texture_resources && cube->refcount == 1 && cube->binding_refs == 1);CHECK(!cube_release(&cube->iface));
        D9VolumeTexture *volume = new_volume(device, 0);CHECK(!device_set_texture(&device->iface, 0, &volume->iface));CHECK(!device->cube_textures && volume->refcount == 1 && volume->binding_refs == 1);CHECK(!volume_texture_release(&volume->iface));
        plain = new_texture(device, 0);CHECK(!device_set_texture(&device->iface, 0, &plain->iface));CHECK(!device->volume_textures && plain->refcount == 1 && plain->binding_refs == 1);CHECK(!texture_release(&plain->iface));
        CHECK(!device_release(&device->iface) && !live && !parent.refs);
    }
    printf("PASS actual D3D9 volume bindings: %u checks, %u pool/order profiles, %u public-zero revivals, 64 mixed-kind replacement cycles; four sampler slots, two cached volume levels, shared public parent count, exact device pins, public-only Reset blockers and complete final-owner cleanup\n", checks - old_checks, profiles, revivals);
    return 0;
}
