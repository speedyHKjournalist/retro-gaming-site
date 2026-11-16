typedef struct Snapshot8Context{Interface*layout,*shader;DWORD fvf;UINT kind;HRESULT result;}Snapshot8Context;
static UINT snapshot8_calls;
static HRESULT WINAPI snapshot8_record(Interface*device,const void*data)
{
    snapshot8_calls++;
    if(!data)return D3D_OK;
    const Snapshot8Context*state=data;
    HRESULT hr=state->kind?d9wg_d3d9_core_set_fvf8(device,state->fvf):d9wg_d3d9_core_set_vertex_shader8(device,state->layout,state->shader);
    return FAILED(hr)?hr:state->result;
}
static int snapshot8_block_cases(void)
{
    UINT before=checks,cases=0;active_compatibility_api=8;
    for(UINT round=0;round<128;round++)for(UINT order=0;order<4;order++)for(UINT ffp=0;ffp<2;ffp++) {
        Parent owner={0};D9Device*d=new_device(&owner);
        D9VertexDeclaration*a=new_declaration(d),*b=new_declaration(d);
        D9Shader*red=new_shader(d,FALSE),*green=ffp?NULL:new_shader(d,FALSE);
        Interface*gi=green?&green->iface.vertex:NULL;
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex));
        CHECK(!decl_release(&a->iface)&&!vertex_shader_release(&red->iface.vertex));
        Snapshot8Context state={&b->iface,gi,0,0,0};Interface*block=(void*)0x11;
        UINT calls=snapshot8_calls,sent=pair_submissions;
        CHECK(!d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&block)&&block&&snapshot8_calls==calls+1);
        CHECK(!d->recording_result&&d->vertex_declaration==a&&d->vertex_shader==red&&pair_submissions==sent);
        CHECK(((D9StateBlock*)block)->has_vertex_format&&((D9StateBlock*)block)->has_vertex_shader&&((D9StateBlock*)block)->vertex_declaration==b&&((D9StateBlock*)block)->vertex_shader==green);
        CHECK(b->binding_refs==1);if(green)CHECK(green->binding_refs==1);
        CHECK(!decl_release(&b->iface));if(green)CHECK(!vertex_shader_release(gi));
        /* Published blocks own copied values and real object references;
         * no callback/context is retained for Apply. */
        memset(&state,0xa5,sizeof(state));calls=snapshot8_calls;
        CHECK(!state_block_apply_method(block)&&d->vertex_declaration==b&&d->vertex_shader==green&&snapshot8_calls==calls);
        Interface*view=NULL,*program=NULL;CHECK(!device_get_vertex_declaration(&d->iface,&view)&&view==&b->iface);
        CHECK(!device_get_vertex_shader(&d->iface,&program)&&program==gi);
        CHECK(!decl_release(view));if(program)CHECK(!vertex_shader_release(program));
        /* Frontend Capture can rebuild the mapped core block from a new
         * raw snapshot while keeping its public DWORD handle unchanged. */
        state=(Snapshot8Context){&b->iface,NULL,0,0,0};Interface*replacement=NULL;
        sent=pair_submissions;CHECK(!d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&replacement)&&replacement&&replacement!=block);
        CHECK(d->vertex_declaration==b&&d->vertex_shader==green&&pair_submissions==sent&&b->binding_refs==3);
        CHECK(!state_block_release(block));block=replacement;
        memset(&state,0,sizeof(state));calls=snapshot8_calls;
        CHECK(!state_block_apply_method(block)&&d->vertex_declaration==b&&!d->vertex_shader&&snapshot8_calls==calls&&b->binding_refs==2);
        if(order==0){CHECK(!state_block_release(block));CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,NULL,NULL)&&!device_release(&d->iface));}
        else if(order==1){device_clear_bindings(d);CHECK(!state_block_release(block)&&!device_release(&d->iface));}
        else if(order==2){view=NULL;CHECK(!device_get_vertex_declaration(&d->iface,&view)&&!state_block_release(block));CHECK(device_release(&d->iface)==1&&!decl_release(view));}
        else {CHECK(device_release(&d->iface)==1&&!state_block_release(block));}
        CHECK(!live&&!owner.refs);cases++;
    }
    Parent owner={0};D9Device*d=new_device(&owner);
    D9VertexDeclaration*a=new_declaration(d),*b=new_declaration(d);
    D9Shader*red=new_shader(d,FALSE),*green=new_shader(d,FALSE);
    CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex));
    Snapshot8Context state={&b->iface,&green->iface.vertex,0,0,-77};Interface*out=(void*)0x11;
    LONG pins=d->refcount;UINT calls=snapshot8_calls,allocated=allocations,alive=live,sent=pair_submissions;
#define SNAPSHOT8_GUARD(dev,record,data,target) do{CHECK(d9wg_d3d9_core_record_state_block8(dev,record,data,target)==D3DERR_INVALIDCALL);CHECK(snapshot8_calls==calls&&d->refcount==pins&&allocations==allocated&&live==alive&&!d->recording_result&&pair_submissions==sent);cases++;}while(0)
    SNAPSHOT8_GUARD(NULL,snapshot8_record,&state,&out);CHECK(!out);
    out=(void*)0x11;SNAPSHOT8_GUARD(&d->iface,NULL,&state,&out);CHECK(!out);
    SNAPSHOT8_GUARD(&d->iface,snapshot8_record,&state,NULL);
    active_compatibility_api=9;out=(void*)0x11;SNAPSHOT8_GUARD(&d->iface,snapshot8_record,&state,&out);CHECK(!out);active_compatibility_api=8;
#undef SNAPSHOT8_GUARD
    fail_allocation=allocations+1;out=(void*)0x11;
    CHECK(d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&out)==E_OUTOFMEMORY&&!out&&snapshot8_calls==calls&&!d->recording_result&&!d->state_blocks&&live==alive&&d->refcount==pins);fail_allocation=0;cases++;
    CHECK(!device_begin_state_block(&d->iface));D9StateBlock*existing=d->recording_result;
    CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex));
    out=(void*)0x11;CHECK(d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&out)==D3DERR_INVALIDCALL&&!out&&snapshot8_calls==calls&&d->recording_result==existing&&existing->has_vertex_shader&&existing->has_vertex_format);
    CHECK(!device_end_state_block(&d->iface,&out)&&!state_block_release(out));cases++;
    out=(void*)0x11;
    CHECK(d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&out)==-77&&!out&&snapshot8_calls==calls+1);
    CHECK(!d->recording_result&&!d->state_blocks&&live==alive&&d->refcount==pins&&d->vertex_declaration==a&&d->vertex_shader==red&&!b->binding_refs&&!green->binding_refs&&pair_submissions==sent);cases++;
    /* Null context is a valid empty snapshot; positive callback status still
     * publishes the normal EndStateBlock success result. */
    CHECK(!d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,NULL,&out)&&out&&!((D9StateBlock*)out)->has_vertex_format&&!((D9StateBlock*)out)->has_vertex_shader);
    CHECK(!state_block_apply_method(out)&&d->vertex_declaration==a&&d->vertex_shader==red&&!state_block_release(out));cases++;
    state.result=1;CHECK(!d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&out)&&out&&((D9StateBlock*)out)->vertex_declaration==b);CHECK(!state_block_release(out));cases++;
    /* A cold FVF may prepare a reachable private cache entry during
     * recording. Abort releases the block but preserves that cache owner. */
    state=(Snapshot8Context){NULL,NULL,0x4042,1,0};fail_allocation=allocations+2;out=(void*)0x11;
    CHECK(d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&out)==E_OUTOFMEMORY&&!out&&!d->recording_result&&!d->state_blocks&&live==alive&&d->refcount==pins);fail_allocation=0;cases++;
    fail_emit=1;out=(void*)0x11;CHECK(d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&out)==D3DERR_DRIVERINTERNALERROR&&!out&&!d->recording_result&&!d->state_blocks&&live==alive&&d->refcount==pins);fail_emit=0;cases++;
    state.result=-77;CHECK(d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&out)==-77&&!out&&!d->recording_result&&!d->state_blocks&&d->vertex_declaration==a&&d->vertex_shader==red&&d->refcount==pins);
    D9VertexDeclaration*cached=d->vertex_declarations;CHECK(cached->legacy_fvf8==0x4042&&cached->fvf_cache_ref&&!cached->refcount&&cached->binding_refs==1);cases++;
    state.result=0;CHECK(!d9wg_d3d9_core_record_state_block8(&d->iface,snapshot8_record,&state,&out)&&out&&((D9StateBlock*)out)->vertex_declaration==cached&&d->vertex_declaration==a);
    CHECK(!state_block_apply_method(out)&&d->vertex_declaration==cached&&!d->vertex_shader&&cached->binding_refs==3);
    CHECK(!state_block_release(out)&&!d9wg_d3d9_core_set_vertex_shader8(&d->iface,NULL,NULL));
    CHECK(!decl_release(&a->iface)&&!decl_release(&b->iface)&&!vertex_shader_release(&red->iface.vertex)&&!vertex_shader_release(&green->iface.vertex));
    CHECK(!device_release(&d->iface)&&!live&&!owner.refs);cases++;
    active_compatibility_api=9;
    printf("PASS actual core8 snapshot blocks: %u checks / %u cases; synchronous frontend callback,real9 recording/publish/abort,detached snapshots/rebuild,program/FFP/public-zero/device-first lifetime,existing recording preserved,callback andallocation/emission failures,coldFVF cache ownership and exact cleanup\n",checks-before,cases);
    return 0;
}
