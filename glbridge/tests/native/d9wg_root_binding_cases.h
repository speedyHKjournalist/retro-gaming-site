static ULONG root_public_add_ref(StateGroup group, UINT kind)
{
    switch (kind) {
    case 0:return texture_add_ref(&group.texture->iface);
    case 1:return cube_add_ref(&group.cube->iface);
    case 2:return volume_texture_add_ref(&group.volume->iface);
    case 3:return vb_add_ref(&group.vb->iface);
    case 4:return ib_add_ref(&group.ib->iface);
    case 5:return decl_add_ref(&group.decl->iface);
    case 6:return vertex_shader_add_ref(&group.vs->iface.vertex);
    default:return pixel_shader_add_ref(&group.ps->iface.pixel);
    }
}
static ULONG root_public_release(StateGroup group, UINT kind)
{
    switch (kind) {
    case 0:return texture_release(&group.texture->iface);
    case 1:return cube_release(&group.cube->iface);
    case 2:return volume_texture_release(&group.volume->iface);
    case 3:return vb_release(&group.vb->iface);
    case 4:return ib_release(&group.ib->iface);
    case 5:return decl_release(&group.decl->iface);
    case 6:return vertex_shader_release(&group.vs->iface.vertex);
    default:return pixel_shader_release(&group.ps->iface.pixel);
    }
}
static HRESULT root_get_device(StateGroup group, UINT kind, Interface **out)
{
    switch (kind) {
    case 0:return texture_get_device(&group.texture->iface,out);
    case 1:return cube_get_device(&group.cube->iface,out);
    case 2:return volume_texture_get_device(&group.volume->iface,out);
    case 3:return vb_get_device(&group.vb->iface,out);
    case 4:return ib_get_device(&group.ib->iface,out);
    case 5:return decl_get_device(&group.decl->iface,out);
    case 6:return vertex_shader_get_device(&group.vs->iface.vertex,out);
    default:return pixel_shader_get_device(&group.ps->iface.pixel,out);
    }
}
static int root_query_group(D9Device *device, StateGroup group)
{
    Interface *out = NULL;UINT offset = 0,stride = 0;
    CHECK(!device_get_texture(&device->iface,0,&out) && out == &group.texture->iface);
    CHECK(!device_get_texture(&device->iface,1,&out) && out == &group.cube->iface);
    CHECK(!device_get_texture(&device->iface,2,&out) && out == &group.volume->iface);
    CHECK(!device_get_stream_source(&device->iface,0,&out,&offset,&stride) && out == &group.vb->iface && offset == 4 && stride == 12);
    CHECK(!device_get_indices(&device->iface,&out) && out == &group.ib->iface);
    CHECK(!device_get_vertex_declaration(&device->iface,&out) && out == &group.decl->iface);
    CHECK(!device_get_vertex_shader(&device->iface,&out) && out == &group.vs->iface.vertex);
    CHECK(!device_get_pixel_shader(&device->iface,&out) && out == &group.ps->iface.pixel);return 0;
}
static int root_binding_cases(void)
{
    UINT old_checks = checks, profiles = 0, recoveries = 0;
    for (UINT round = 0; round < 16; round++) for (UINT pool = 0; pool < 4; pool++) for (UINT kind = 0; kind < 8; kind++) for (UINT order = 0; order < 4; order++) {
        Parent parent = {0};D9Device *device = new_device(&parent);StateGroup group = new_state_group(device,pool);
        CHECK(!bind_state_group(device,group) && !release_state_group(group) && !state_group_refs(group,0,1));
        device->render_states[7] = 0x12345678;device->vs_const_f[3][2] = .75f;
        CHECK(root_public_add_ref(group,kind) == 1 && device->refcount == 2 && device->child_parent_refs == 1);
        UINT old_destroys = destroys;
        D9StateBlock *recording = NULL;
        if (order == 3) {
            CHECK(!device_begin_state_block(&device->iface) && device->refcount == 2 && device->child_parent_refs == 1);
            recording = device->recording_result;CHECK(recording && !recording->device_parent_ref);
            CHECK(!bind_state_group(device,group));
        }
        CHECK(device_release(&device->iface) == 1 && destroys == old_destroys);
        CHECK(device->textures[0] == group.texture && device->cube_bindings[1] == group.cube && device->volume_bindings[2] == group.volume && device->streams[0].buffer == group.vb && device->index_buffer == group.ib && device->vertex_declaration == group.decl && device->vertex_shader == group.vs && device->pixel_shader == group.ps);
        CHECK(device->render_states[7] == 0x12345678 && device->vs_const_f[3][2] == .75f && device->recording_result == recording);
        Interface *owner = NULL;CHECK(!root_get_device(group,kind,&owner) && owner == &device->iface && device->refcount == 2);
        CHECK(!root_query_group(device,group) && device->refcount == 9 && device->child_parent_refs == 8);
        for (UINT i = 0; i < 8; i++) CHECK(root_public_release(group,i) == (i == kind ? 1u : 0u));
        CHECK(device->refcount == 2 && device->child_parent_refs == 1 && destroys == old_destroys);
        recoveries++;
        if (order == 0) {
            CHECK(device_release(owner) == 1 && destroys == old_destroys);
            CHECK(!root_public_release(group,kind) && !live && !parent.refs && destroys == old_destroys + 8);
        } else if (order == 1) {
            CHECK(!root_public_release(group,kind) && device->refcount == 1 && !state_group_refs(group,0,1));
            CHECK(!device_release(owner) && !live && !parent.refs && destroys == old_destroys + 8);
        } else if (order == 2) {
            device_clear_bindings(device);CHECK(destroys == old_destroys + 7 && device->refcount == 2);
            CHECK(device_release(owner) == 1 && !root_public_release(group,kind) && !live && !parent.refs && destroys == old_destroys + 8);
        } else {
            Interface *block = NULL;CHECK(!device_end_state_block(owner,&block) && block == &recording->iface && recording->device_parent_ref && device->refcount == 3);
            CHECK(!root_public_release(group,kind) && device->refcount == 2);
            CHECK(device_release(owner) == 1 && destroys == old_destroys && !state_group_refs(group,0,2));
            CHECK(!state_block_apply_method(block) && !state_group_refs(group,0,2));
            CHECK(!state_block_release(block) && !live && !parent.refs && destroys == old_destroys + 8);
        }
        profiles++;
    }
    printf("PASS actual D3D9 external-child device state: %u checks, %u pool/anchor/final-owner profiles, %u GetDevice recoveries; eight public anchor families preserve all live slots and scalar state, recording stays private until End, exact device pins and final cleanup\n",checks-old_checks,profiles,recoveries);
    return 0;
}
