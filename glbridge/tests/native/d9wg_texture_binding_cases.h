static D9Texture *new_texture(D9Device *device, UINT pool)
{
    D9Texture *texture = HeapAlloc(NULL, 8, sizeof(*texture));
    texture->iface.lpVtbl = &g_texture_vtbl;
    texture->refcount = 1;texture->device = device;texture->handle = allocate_handle();
    texture->width = 32;texture->height = 24;texture->level_count = 1;
    texture->format = 22;texture->pool = pool;texture->usage = 1;
    texture->levels = HeapAlloc(NULL, 8, sizeof(*texture->levels));
    texture->levels[0].width = 32;texture->levels[0].height = 24;
    texture->levels[0].shadow = HeapAlloc(NULL, 8, 32 * 24 * 4);
    texture->next_device_resource = device->texture_resources;
    device->texture_resources = texture;device_child_add_ref(device);
    return texture;
}
static int texture_binding_cases(void)
{
    const UINT stages[] = {0, 15, 257, 260};
    UINT profiles = 0, revivals = 0, old_checks = checks;
    for (UINT round = 0; round < 64; round++) for (UINT pool = 0; pool < 4; pool++) for (UINT order = 0; order < 4; order++) {
        Parent parent = {0};D9Device *device = new_device(&parent);
        D9Texture *texture = new_texture(device, pool);Interface *view = NULL;
        CHECK(device->refcount == 2 && device->child_parent_refs == 1);
        for (UINT i = 0; i < 4; i++) CHECK(!device_set_texture(&device->iface, stages[i], &texture->iface));
        CHECK(texture->refcount == 1 && texture->binding_refs == 4 && device->refcount == 2);
        CHECK(!texture_get_surface_level(&texture->iface, 0, &view) && texture->refcount == 2);
        CHECK(!device_set_render_target(&device->iface, 3, view) && !device_set_render_target(&device->iface, 2, view));
        CHECK(texture->refcount == 2 && texture->binding_refs == 6 && device->refcount == 2);
        CHECK(surface_release(view) == 1 && !texture_release(&texture->iface));
        CHECK(!texture->refcount && texture->binding_refs == 6 && device->refcount == 1 && !device->child_parent_refs);
        CHECK(!texture_release(&texture->iface) && texture->binding_refs == 6);
        CHECK(!device_has_reset_blockers(device));
        for (UINT cycle = 0; cycle < 8; cycle++) {
            Interface *queried = NULL, *target = NULL;
            CHECK(!device_get_texture(&device->iface, stages[cycle % 4], &queried) && queried == &texture->iface);
            CHECK(texture->refcount == 1 && device->refcount == 2 && device->child_parent_refs == 1);
            CHECK(device_has_reset_blockers(device) == (pool == 0));
            CHECK(!device_get_render_target(&device->iface, 3, &target) && target == view && texture->refcount == 2 && device->refcount == 2);
            CHECK(texture_add_ref(queried) == 3 && surface_release(target) == 2 && texture_release(queried) == 1);
            CHECK(!texture_release(queried) && device->refcount == 1 && !device_has_reset_blockers(device));
            CHECK(!device_set_texture(&device->iface, stages[cycle % 4], queried) && texture->binding_refs == 6);
            CHECK(!device_set_render_target(&device->iface, 3, view) && texture->binding_refs == 6);
            revivals++;
        }
        texture->levels[0].locked = TRUE;CHECK(device_has_reset_blockers(device) == (pool == 0));texture->levels[0].locked = FALSE;
        Interface *sentinel = (void *)1;LONG refs = texture->binding_refs;
        CHECK(device_get_texture(&device->iface, 256, &sentinel) == -1 && sentinel == (void *)1);
        CHECK(device_get_texture(&device->iface, 0, NULL) == -1 && device_set_texture(&device->iface, 256, &texture->iface) == -1);
        D9Device foreign = {0};D9Texture other = {.iface = {&g_texture_vtbl}, .device = &foreign};
        CHECK(device_set_texture(&device->iface, 0, &other.iface) == -1 && texture->binding_refs == refs && device->textures[0] == texture);
        UINT old_destroys = destroys;
        if (order == 0) {
            for (UINT i = 0; i < 4; i++) CHECK(!device_set_texture(&device->iface, stages[i], NULL));
            CHECK(texture->binding_refs == 2 && !texture->refcount);
            CHECK(!device_set_render_target(&device->iface, 2, NULL));
            CHECK(!device_set_render_target(&device->iface, 3, NULL) && destroys == old_destroys + 1 && !device->texture_resources && live == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        } else if (order == 1) {
            CHECK(!device_release(&device->iface) && !live && !parent.refs && destroys == old_destroys + 1);
        } else if (order == 2) {
            Interface *queried = NULL;CHECK(!device_get_texture(&device->iface, 0, &queried));
            device_clear_bindings(device);CHECK(texture->refcount == 1 && !texture->binding_refs && device->refcount == 2 && destroys == old_destroys);
            CHECK(device_release(&device->iface) == 1);CHECK(!texture_release(queried) && !live && !parent.refs && destroys == old_destroys + 1);
        } else {
            device_clear_bindings(device);CHECK(device->refcount == 1 && !device->texture_resources && destroys == old_destroys + 1 && live == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        }
        profiles++;
    }
    printf("PASS actual D3D9 texture bindings: %u checks, %u pool/order profiles, %u public-zero revivals; four sampler slots plus two texture-level targets, shared public parent count, exact device pin transitions, private-only Reset and complete explicit/device-first/last-public cleanup\n", checks - old_checks, profiles, revivals);
    return 0;
}
