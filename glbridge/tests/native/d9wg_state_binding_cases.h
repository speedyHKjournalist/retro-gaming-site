typedef struct StateGroup {
    D9Texture *texture;D9CubeTexture *cube;D9VolumeTexture *volume;
    D9VertexBuffer *vb;D9IndexBuffer *ib;D9VertexDeclaration *decl;D9Shader *vs,*ps;
} StateGroup;
static StateGroup new_state_group(D9Device *device, UINT pool)
{
    return (StateGroup){new_texture(device,pool),new_cube(device,pool),new_volume(device,pool),new_buffer(device,pool,FALSE),new_buffer(device,pool,TRUE),new_declaration(device),new_shader(device,FALSE),new_shader(device,TRUE)};
}
static int state_group_refs(StateGroup group, LONG public_refs, LONG private_refs)
{
    CHECK(group.texture->refcount == public_refs && group.texture->binding_refs == private_refs);
    CHECK(group.cube->refcount == public_refs && group.cube->binding_refs == private_refs);
    CHECK(group.volume->refcount == public_refs && group.volume->binding_refs == private_refs);
    CHECK(group.vb->refcount == public_refs && group.vb->binding_refs == private_refs);
    CHECK(group.ib->refcount == public_refs && group.ib->binding_refs == private_refs);
    CHECK(group.decl->refcount == public_refs && group.decl->binding_refs == private_refs);
    CHECK(group.vs->refcount == public_refs && group.vs->binding_refs == private_refs);
    CHECK(group.ps->refcount == public_refs && group.ps->binding_refs == private_refs);return 0;
}
static int release_state_group(StateGroup group)
{
    CHECK(!texture_release(&group.texture->iface) && !cube_release(&group.cube->iface) && !volume_texture_release(&group.volume->iface));
    CHECK(!vb_release(&group.vb->iface) && !ib_release(&group.ib->iface) && !decl_release(&group.decl->iface));
    CHECK(!vertex_shader_release(&group.vs->iface.vertex) && !pixel_shader_release(&group.ps->iface.pixel));return 0;
}
static int bind_state_group(D9Device *device, StateGroup group)
{
    CHECK(!device_set_texture(&device->iface,0,&group.texture->iface));
    CHECK(!device_set_texture(&device->iface,1,&group.cube->iface));
    CHECK(!device_set_texture(&device->iface,2,&group.volume->iface));
    CHECK(!device_set_stream_source(&device->iface,0,&group.vb->iface,4,12));
    CHECK(!device_set_indices(&device->iface,&group.ib->iface));
    CHECK(!device_set_vertex_declaration(&device->iface,&group.decl->iface));
    CHECK(!device_set_vertex_shader(&device->iface,&group.vs->iface.vertex));
    CHECK(!device_set_pixel_shader(&device->iface,&group.ps->iface.pixel));return 0;
}
static int state_binding_cases(void)
{
    UINT old_checks = checks, profiles = 0, captures = 0;
    for (UINT round = 0; round < 32; round++) for (UINT pool = 0; pool < 4; pool++) for (UINT order = 0; order < 4; order++) {
        Parent parent = {0};D9Device *device = new_device(&parent);StateGroup group = new_state_group(device,pool);
        UINT old_destroys = destroys;
        CHECK(!device_begin_state_block(&device->iface) && device->refcount == 9);
        CHECK(!bind_state_group(device,group) && !state_group_refs(group,1,1));
        CHECK(!bind_state_group(device,group) && !state_group_refs(group,1,1));
        CHECK(!device_set_texture(&device->iface,0,&group.cube->iface) && !device_set_texture(&device->iface,0,&group.volume->iface) && !device_set_texture(&device->iface,0,&group.texture->iface));
        CHECK(!device_set_fvf(&device->iface,2) && !group.decl->binding_refs && !device_set_vertex_declaration(&device->iface,&group.decl->iface));
        CHECK(!state_group_refs(group,1,1) && !device->textures[0] && !device->vertex_shader && !device->vertex_declaration && !device->streams[0].buffer && !device->index_buffer);
        CHECK(!release_state_group(group) && device->refcount == 1 && !device->child_parent_refs && !device_has_reset_blockers(device));
        Interface *out = NULL;CHECK(!device_end_state_block(&device->iface,&out));D9StateBlock *block = (D9StateBlock*)out;
        CHECK(block && block == device->state_blocks && !device->recording_result);
        CHECK(!state_block_apply_method(out) && !state_group_refs(group,0,2));
        for (UINT cycle = 0; cycle < 8; cycle++) {
            CHECK(!state_block_capture_method(out) && !state_group_refs(group,0,2));
            device_clear_bindings(device);CHECK(!state_group_refs(group,0,1) && !device_has_reset_blockers(device));
            CHECK(!state_block_apply_method(out) && !state_group_refs(group,0,2));
            CHECK(device->refcount == 2 && device->child_parent_refs == 1 && destroys == old_destroys);
            captures++;
        }
        CHECK(!device_begin_state_block(&device->iface));
        CHECK(state_block_capture_method(out) == -1 && state_block_apply_method(out) == -1 && !state_group_refs(group,0,2));
        Interface *empty = NULL;CHECK(!device_end_state_block(&device->iface,&empty) && !state_block_release(empty));
        device_clear_bindings(device);CHECK(!state_group_refs(group,0,1) && !device_has_reset_blockers(device));
        if (order == 0) {
            CHECK(!state_block_release(out) && destroys == old_destroys + 8 && device->refcount == 1 && !device->state_blocks);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        } else if (order == 1) {
            CHECK(device_release(&device->iface) == 1 && !state_block_release(out) && !live && !parent.refs && destroys == old_destroys + 9);
        } else if (order == 2) {
            CHECK(!state_block_capture_method(out) && destroys == old_destroys + 8 && !block->textures[0] && !block->cube_textures[1] && !block->volume_textures[2] && !block->vertex_shader && !block->pixel_shader && !block->vertex_declaration && !block->streams[0].buffer && !block->index_buffer);
            CHECK(!state_block_apply_method(out) && !state_block_release(out) && device->refcount == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        } else {
            StateGroup replacement = new_state_group(device,pool);CHECK(!bind_state_group(device,replacement) && !release_state_group(replacement));
            CHECK(!state_block_capture_method(out) && destroys == old_destroys + 8 && !state_group_refs(replacement,0,2));
            CHECK(!state_block_release(out) && !state_group_refs(replacement,0,1));
            CHECK(!device_release(&device->iface) && !live && !parent.refs && destroys == old_destroys + 17);
        }
        profiles++;
    }
    /* Capture refreshes storage/stride while predefined blocks retain their
     * creation offsets. Recorded blocks refresh offsets on every capture. */
    for(UINT frozen=0;frozen<2;frozen++) {
        Parent p={0};D9Device*d=new_device(&p);D9VertexBuffer*a=new_buffer(d,0,FALSE),*b=new_buffer(d,0,FALSE);
        D9StateBlock*block=state_block_alloc(d,TRUE);CHECK(block);
        for(UINT i=0;i<16;i++){CHECK(!device_set_stream_source(&d->iface,i,&a->iface,4*(i%4),8));block->has_stream[i]=TRUE;}
        state_block_capture(block);block->freeze_stream_offsets=frozen;
        for(UINT round=0;round<3;round++) {
            for(UINT i=0;i<16;i++)CHECK(!device_set_stream_source(&d->iface,i,&b->iface,16+4*round,12+4*round));
            CHECK(!state_block_capture_method(&block->iface));
            device_clear_bindings(d);CHECK(!state_block_apply_method(&block->iface));
            for(UINT i=0;i<16;i++) {
                Interface*got=NULL;UINT offset=99,stride=99;
                CHECK(!device_get_stream_source(&d->iface,i,&got,&offset,&stride));
                CHECK(got==&b->iface&&offset==(frozen?4*(i%4):16+4*round)&&stride==12+4*round);
                CHECK(vb_release(got)==1);
            }
        }
        CHECK(!a->binding_refs&&b->binding_refs==32);device_clear_bindings(d);
        CHECK(!state_block_release(&block->iface)&&!vb_release(&a->iface)&&!vb_release(&b->iface));
        CHECK(!device_release(&d->iface)&&!live&&!p.refs);
    }
    /* An unfinished recording owns private resources and is cancelled when
     * the device's last external reference is dropped. */
    Parent parent = {0};D9Device *device = new_device(&parent);StateGroup group = new_state_group(device,0);UINT old_destroys = destroys;
    CHECK(!device_begin_state_block(&device->iface) && !bind_state_group(device,group) && !release_state_group(group));
    CHECK(!device_release(&device->iface) && !live && !parent.refs && destroys == old_destroys + 8);
    printf("PASS actual D3D9 stateblock private bindings: %u checks, %u pool/final-owner profiles, %u capture/clear/apply cycles; eight resource families, recorded replacement/FVF, public-zero storage, recording guards, recapture retirement and unfinished-recording cleanup\n",checks-old_checks,profiles,captures);
    return 0;
}
