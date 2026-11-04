/* The constructors below provide object metadata. Binding, state-block,
 * public/private references, queries and destruction are actual core methods. */
static int program8_binding_cases(void)
{
    UINT before=checks,cases=0;active_compatibility_api=8;
    for(UINT round=0;round<128;round++)for(UINT order=0;order<4;order++)for(UINT ffp=0;ffp<2;ffp++) {
        Parent owner={0};D9Device*d=new_device(&owner);
        D9VertexDeclaration*a=new_declaration(d),*b=new_declaration(d);
        D9Shader*red=new_shader(d,FALSE),*green=ffp?NULL:new_shader(d,FALSE);
        Interface*gi=green?&green->iface.vertex:NULL;
        UINT sent=pair_submissions;
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex));
        CHECK(d->vertex_declaration==a&&d->vertex_shader==red&&pair_submissions==sent+2);
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&b->iface,&red->iface.vertex)&&d->vertex_declaration==b&&d->vertex_shader==red);
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex)&&d->vertex_declaration==a&&d->vertex_shader==red);
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,gi)&&d->vertex_declaration==a&&d->vertex_shader==green);
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex)&&d->vertex_declaration==a&&d->vertex_shader==red);
        CHECK(!decl_release(&a->iface)&&!vertex_shader_release(&red->iface.vertex));
        sent=pair_submissions;CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex)&&pair_submissions==sent&&a->binding_refs==1&&red->binding_refs==1);
        CHECK(!device_begin_state_block(&d->iface));
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&b->iface,gi)&&pair_submissions==sent&&d->vertex_declaration==a&&d->vertex_shader==red);
        CHECK(d->recording_result->has_vertex_format&&d->recording_result->has_vertex_shader&&d->recording_result->vertex_declaration==b&&d->recording_result->vertex_shader==green);
        Interface*block=NULL;CHECK(!device_end_state_block(&d->iface,&block));
        CHECK(!decl_release(&b->iface));if(green)CHECK(!vertex_shader_release(gi));
        CHECK(!state_block_apply_method(block)&&d->vertex_declaration==b&&d->vertex_shader==green&&b->binding_refs==2);
        if(green)CHECK(green->binding_refs==2);
        /* Recording an identical live pair must still record both slots. */
        sent=pair_submissions;CHECK(!device_begin_state_block(&d->iface));
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&b->iface,gi)&&pair_submissions==sent);
        CHECK(d->recording_result->has_vertex_format&&d->recording_result->has_vertex_shader&&d->recording_result->vertex_declaration==b&&d->recording_result->vertex_shader==green);
        Interface*same=NULL;CHECK(!device_end_state_block(&d->iface,&same)&&!state_block_release(same));
        Interface*view=NULL,*program=NULL;CHECK(!device_get_vertex_declaration(&d->iface,&view)&&view==&b->iface&&b->refcount==1);
        CHECK(!device_get_vertex_shader(&d->iface,&program)&&program==gi);
        CHECK(!decl_release(view));if(program)CHECK(!vertex_shader_release(program));
        /* Recorded NULL/NULL delegates to the8 zero semantics, without live mutation. */
        CHECK(!device_begin_state_block(&d->iface));sent=pair_submissions;
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,NULL,NULL)&&pair_submissions==sent&&d->vertex_declaration==b&&d->vertex_shader==green);
        Interface*zero=NULL;CHECK(!device_end_state_block(&d->iface,&zero));
        CHECK(!state_block_apply_method(zero)&&!d->vertex_declaration&&!d->vertex_shader&&b->binding_refs==1);
        CHECK(!state_block_release(zero)&&!state_block_apply_method(block));
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&b->iface,NULL)&&d->vertex_declaration==b&&!d->vertex_shader);
        CHECK(!state_block_apply_method(block));
        if(order==0){CHECK(!state_block_release(block));CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,NULL,NULL)&&!device_release(&d->iface));}
        else if(order==1){device_clear_bindings(d);CHECK(!state_block_release(block)&&!device_release(&d->iface));}
        else if(order==2){view=program=NULL;CHECK(!device_get_vertex_declaration(&d->iface,&view)&&!device_get_vertex_shader(&d->iface,&program));CHECK(!state_block_release(block));CHECK(device_release(&d->iface)==(green?2:1));CHECK(!decl_release(view));if(program)CHECK(!vertex_shader_release(program));}
        else {CHECK(device_release(&d->iface)==1&&!state_block_release(block));}
        CHECK(!live&&!owner.refs);cases++;
    }
    Parent owner={0};D9Device*d=new_device(&owner);
    D9VertexDeclaration*a=new_declaration(d),*b=new_declaration(d);
    D9Shader*red=new_shader(d,FALSE),*green=new_shader(d,FALSE),*pixel=new_shader(d,TRUE);
    CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex));
    D9VertexDeclaration foreign_layout=*b;D9Shader foreign_shader=*green;int alien_vtable;
    D9Device alien_device={0};UINT sent=pair_submissions,allocated=allocations;
#define REJECT_PAIR(dev,layout,shader) do{CHECK(d9wg_d3d9_core_set_vertex_shader8(dev,layout,shader)==D3DERR_INVALIDCALL);CHECK(d->vertex_declaration==a&&d->vertex_shader==red&&pair_submissions==sent&&allocations==allocated);cases++;}while(0)
    REJECT_PAIR(NULL,&b->iface,&green->iface.vertex);
    active_compatibility_api=9;REJECT_PAIR(&d->iface,&b->iface,&green->iface.vertex);REJECT_PAIR(&d->iface,NULL,NULL);active_compatibility_api=8;
    REJECT_PAIR(&d->iface,NULL,&green->iface.vertex);
    foreign_layout.iface.lpVtbl=&alien_vtable;REJECT_PAIR(&d->iface,&foreign_layout.iface,&green->iface.vertex);
    foreign_layout=*b;foreign_layout.device=&alien_device;REJECT_PAIR(&d->iface,&foreign_layout.iface,&green->iface.vertex);
    foreign_shader.iface.vertex.lpVtbl=&alien_vtable;REJECT_PAIR(&d->iface,&b->iface,&foreign_shader.iface.vertex);
    foreign_shader=*green;foreign_shader.device=&alien_device;REJECT_PAIR(&d->iface,&b->iface,&foreign_shader.iface.vertex);
    REJECT_PAIR(&d->iface,&b->iface,&pixel->iface.pixel);
    CHECK(!device_begin_state_block(&d->iface));allocated=allocations;REJECT_PAIR(&d->iface,&b->iface,&foreign_shader.iface.vertex);
    CHECK(!d->recording_result->has_vertex_format&&!d->recording_result->has_vertex_shader);
    Interface*empty=NULL;CHECK(!device_end_state_block(&d->iface,&empty)&&!state_block_release(empty));
#undef REJECT_PAIR
    /* Core9 setters publish shadow state before transport. Preserve that
     * explicit failure contract; never replay an uncertain accepted prefix. */
    pair_fail_submission=pair_submissions+1;
    CHECK(d9wg_d3d9_core_set_vertex_shader8(&d->iface,&b->iface,&green->iface.vertex)==D3DERR_DRIVERINTERNALERROR);
    CHECK(pair_submissions==pair_fail_submission&&d->vertex_declaration==b&&d->vertex_shader==red);
    pair_fail_submission=0;CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex));
    pair_fail_submission=pair_submissions+2;
    CHECK(d9wg_d3d9_core_set_vertex_shader8(&d->iface,&b->iface,&green->iface.vertex)==D3DERR_DRIVERINTERNALERROR);
    CHECK(pair_submissions==pair_fail_submission&&d->vertex_declaration==b&&d->vertex_shader==green);
    pair_fail_submission=0;CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,NULL,NULL));
    CHECK(!decl_release(&a->iface)&&!decl_release(&b->iface)&&!vertex_shader_release(&red->iface.vertex)&&!vertex_shader_release(&green->iface.vertex)&&!pixel_shader_release(&pixel->iface.pixel));
    CHECK(!device_release(&d->iface)&&!live&&!owner.refs);cases+=2;
    active_compatibility_api=9;
    printf("PASS actual core8 program pair bindings: %u checks / %u cases; prevalidated owners/types,program/FFP/zero,duplicate versus recording,actual state-block/public-zero/device-first lifetime,submission-prefix failures and exact cleanup\n",checks-before,cases);
    return 0;
}
