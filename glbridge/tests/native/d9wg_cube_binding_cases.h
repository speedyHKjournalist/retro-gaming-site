static D9CubeTexture *new_cube(D9Device *device, UINT pool)
{
    D9CubeTexture *texture = HeapAlloc(NULL, 8, sizeof(*texture));
    texture->iface.lpVtbl = &g_cube_vtbl;
    texture->refcount = 1;texture->device = device;texture->handle = allocate_handle();
    texture->width = 32;texture->height = 32;texture->level_count = 1;
    texture->format = 22;texture->pool = pool;texture->usage = 1;
    texture->levels = HeapAlloc(NULL, 8, 6 * sizeof(*texture->levels));
    for (UINT face = 0; face < 6; face++) {texture->levels[face].width = texture->levels[face].height = 32;texture->levels[face].shadow = HeapAlloc(NULL, 8, 32 * 32 * 4);}
    texture->next_device_resource = device->cube_textures;
    device->cube_textures = texture;device_child_add_ref(device);
    return texture;
}
static int cube_binding_cases(void)
{
    const UINT stages[] = {0, 15, 257, 260};
    UINT profiles = 0, revivals = 0, old_checks = checks;
    for (UINT round = 0; round < 64; round++) for (UINT pool = 0; pool < 4; pool++) for (UINT order = 0; order < 4; order++) {
        Parent parent = {0};D9Device *device = new_device(&parent);
        D9CubeTexture *texture = new_cube(device, pool);Interface *view = NULL;
        CHECK(device->refcount == 2 && device->child_parent_refs == 1);
        for (UINT table = 0; table < 2; table++) {
            texture->iface.lpVtbl = table ? (void *)(uintptr_t)0xdeadbeef : NULL;
            CHECK(!device_set_texture(&device->iface, 0, &texture->iface));
            CHECK(texture->refcount == 1 && texture->binding_refs == 1);
            Interface unknown = {texture->iface.lpVtbl};
            CHECK(device_set_texture(&device->iface, 0, &unknown) == -1);
            CHECK(!device_set_texture(&device->iface, 0, NULL) && !texture->binding_refs);
        }
        texture->iface.lpVtbl = &g_cube_vtbl;
        for (UINT i = 0; i < 4; i++) CHECK(!device_set_texture(&device->iface, stages[i], &texture->iface));
        CHECK(texture->refcount == 1 && texture->binding_refs == 4 && device->refcount == 2);
        for (UINT face = 0; face < 6; face++) {Interface *q = NULL; CHECK(!cube_get_cube_map_surface(&texture->iface, face, 0, &q)); CHECK(((D9Surface *)q)->texture_child && !((D9Surface *)q)->texture && ((D9Surface *)q)->cube_texture == texture); if (face == round % 6) view = q; else CHECK(surface_release(q) >= 1);}
        CHECK(view && texture->refcount == 2);
        CHECK(!device_set_render_target(&device->iface, 3, view) && !device_set_render_target(&device->iface, 2, view));
        CHECK(texture->refcount == 2 && texture->binding_refs == 6 && device->refcount == 2);
        CHECK(surface_release(view) == 1 && !cube_release(&texture->iface));
        CHECK(!texture->refcount && texture->binding_refs == 6 && device->refcount == 1 && !device->child_parent_refs);
        CHECK(!cube_release(&texture->iface) && texture->binding_refs == 6);
        CHECK(!device_has_reset_blockers(device));
        for (UINT cycle = 0; cycle < 8; cycle++) {
            Interface *queried = NULL, *target = NULL;
            CHECK(!device_get_texture(&device->iface, stages[cycle % 4], &queried) && queried == &texture->iface);
            CHECK(texture->refcount == 1 && device->refcount == 2 && device->child_parent_refs == 1);
            CHECK(device_has_reset_blockers(device) == (pool == 0));
            CHECK(!device_get_render_target(&device->iface, 3, &target) && target == view && texture->refcount == 2 && device->refcount == 2);
            CHECK(cube_add_ref(queried) == 3 && surface_release(target) == 2 && cube_release(queried) == 1);
            CHECK(!cube_release(queried) && device->refcount == 1 && !device_has_reset_blockers(device));
            CHECK(!device_set_texture(&device->iface, stages[cycle % 4], queried) && texture->binding_refs == 6);
            CHECK(!device_set_render_target(&device->iface, 3, view) && texture->binding_refs == 6);
            revivals++;
        }
        texture->levels[0].locked = TRUE;CHECK(device_has_reset_blockers(device) == (pool == 0));texture->levels[0].locked = FALSE;
        Interface *sentinel = (void *)1;LONG refs = texture->binding_refs;
        CHECK(device_get_texture(&device->iface, 256, &sentinel) == -1 && sentinel == (void *)1);
        CHECK(device_get_texture(&device->iface, 0, NULL) == -1 && device_set_texture(&device->iface, 256, &texture->iface) == -1);
        D9Device foreign = {0};D9CubeTexture other = {.iface = {&g_cube_vtbl}, .device = &foreign};
        CHECK(device_set_texture(&device->iface, 0, &other.iface) == -1 && texture->binding_refs == refs && device->cube_bindings[0] == texture);
        UINT old_destroys = destroys;
        if (order == 0) {
            for (UINT i = 0; i < 4; i++) CHECK(!device_set_texture(&device->iface, stages[i], NULL));
            CHECK(texture->binding_refs == 2 && !texture->refcount);
            CHECK(!device_set_render_target(&device->iface, 2, NULL));
            CHECK(!device_set_render_target(&device->iface, 3, NULL) && destroys == old_destroys + 1 && !device->cube_textures && live == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        } else if (order == 1) {
            CHECK(!device_release(&device->iface) && !live && !parent.refs && destroys == old_destroys + 1);
        } else if (order == 2) {
            Interface *queried = NULL;CHECK(!device_get_texture(&device->iface, 0, &queried));
            device_clear_bindings(device);CHECK(texture->refcount == 1 && !texture->binding_refs && device->refcount == 2 && destroys == old_destroys);
            CHECK(device_release(&device->iface) == 1);CHECK(!cube_release(queried) && !live && !parent.refs && destroys == old_destroys + 1);
        } else {
            device_clear_bindings(device);CHECK(device->refcount == 1 && !device->cube_textures && destroys == old_destroys + 1 && live == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        }
        profiles++;
    }
    printf("PASS actual D3D9 cube bindings: %u checks, %u pool/order profiles, %u public-zero revivals; four sampler slots plus two texture-level targets, six actual cached face constructors and shared public parent count, exact device pin transitions, private-only Reset and complete explicit/device-first/last-public cleanup\n", checks - old_checks, profiles, revivals);
    return 0;
}
