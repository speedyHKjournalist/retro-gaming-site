#define TOKEN_CREATE d9wg_d3d9_core_create_state_block_handle8
#define TOKEN_APPLY d9wg_d3d9_core_apply_state_block_handle8
#define TOKEN_CAPTURE d9wg_d3d9_core_capture_state_block_handle8
#define TOKEN_DELETE d9wg_d3d9_core_delete_state_block_handle8
/* Real private-data finalizers exercise detach-before-callback and device
 * revival. Operations under test retain real block/device storage. */
typedef struct TokenPrivate { D9Device *device;DWORD token,created,peer;UINT refs,calls,mode;LONG pins,block_pins;HRESULT deleted,applied,create_result,peer_apply,peer_delete; } TokenPrivate;
static void*token_private_alloc(void*ctx,uint32_t n){(void)ctx;return HeapAlloc(NULL,8,n);}
static void token_private_free(void*ctx,void*p){(void)ctx;HeapFree(NULL,0,p);}
static void token_private_retain(void*ctx,void*p){(void)ctx;((TokenPrivate*)p)->refs++;}
static void token_private_release(void*ctx,void*p){
    (void)ctx;TokenPrivate*x=p;assert(x->refs);x->refs--;x->calls++;
    x->pins=x->device->refcount;
    D9StateBlock*held=state_block_handle8(x->device,x->token);x->block_pins=held?held->refcount:0;
    if(x->mode==3&&x->block_pins!=2)return;
    if(x->peer){x->peer_apply=TOKEN_APPLY(&x->device->iface,x->peer);x->peer_delete=TOKEN_DELETE(&x->device->iface,x->peer);}
    x->deleted=TOKEN_DELETE(&x->device->iface,x->token);
    x->applied=TOKEN_APPLY(&x->device->iface,x->token);
    x->created=0x1234;
    x->create_result=TOKEN_CREATE(&x->device->iface,snapshot8_record,NULL,&x->created);
    if(x->mode==2)device_add_ref(&x->device->iface);
}
static HRESULT WINAPI token8_group_record(Interface*iface,const void*context){
    StateGroup*g=(StateGroup*)context;return bind_state_group((D9Device*)iface,*g);
}
typedef struct TokenDeleteContext {Snapshot8Context state;DWORD token;HRESULT result;}TokenDeleteContext;
static HRESULT WINAPI token8_delete_record(Interface*iface,const void*context){
    TokenDeleteContext*x=(TokenDeleteContext*)context;
    x->result=TOKEN_DELETE(iface,x->token);
    return snapshot8_record(iface,&x->state);
}
typedef struct TokenPinContext {DWORD token;LONG expected_device;BOOL capture;}TokenPinContext;
static HRESULT WINAPI token8_pin_record(Interface*iface,const void*context){
    const TokenPinContext*x=context;D9Device*d=(D9Device*)iface;
    if(d->refcount!=x->expected_device)return -99;
    if(x->capture&&(!state_block_handle8(d,x->token)||state_block_handle8(d,x->token)->refcount!=2))return -98;
    return D3D_OK;
}
static int token8_block_cases(void)
{
    UINT before=checks,cases=0;active_compatibility_api=8;DWORD previous=0;
    for(UINT round=0;round<128;round++)for(UINT order=0;order<4;order++)for(UINT ffp=0;ffp<2;ffp++){
        Parent owner={0};D9Device*d=new_device(&owner);DWORD token=0;
        if(previous)CHECK(TOKEN_APPLY(&d->iface,previous)==D3DERR_INVALIDCALL);
        D9VertexDeclaration*a=new_declaration(d),*b=new_declaration(d);
        D9Shader*red=new_shader(d,FALSE),*green=ffp?NULL:new_shader(d,FALSE);
        CHECK(!d9wg_d3d9_core_set_vertex_shader8(&d->iface,&a->iface,&red->iface.vertex));
        Snapshot8Context state={&b->iface,green?&green->iface.vertex:NULL,0,0,0};
        LONG pins=d->refcount,children=d->child_parent_refs;UINT sent=pair_submissions;
        CHECK(!TOKEN_CREATE(&d->iface,snapshot8_record,&state,&token)&&token&&token!=previous);
        CHECK(d->refcount==pins&&d->child_parent_refs==children&&!d->recording_result&&pair_submissions==sent);
        D9StateBlock*old=state_block_handle8(d,token);CHECK(old&&old->refcount==1&&!old->device_parent_ref&&old->vertex_declaration==b);
        CHECK(!decl_release(&a->iface)&&!decl_release(&b->iface)&&!vertex_shader_release(&red->iface.vertex));if(green)CHECK(!vertex_shader_release(&green->iface.vertex));
        CHECK(d->refcount==1&&!d->child_parent_refs);
        memset(&state,0xa5,sizeof(state));UINT calls=snapshot8_calls;
        CHECK(!TOKEN_APPLY(&d->iface,token)&&d->vertex_declaration==b&&d->vertex_shader==green&&snapshot8_calls==calls&&d->refcount==1);
        /* Rebuild from a new frontend snapshot, preserving token identity. */
        state=(Snapshot8Context){&b->iface,NULL,0,0,0};
        CHECK(!TOKEN_CAPTURE(&d->iface,token,snapshot8_record,&state));
        CHECK(state_block_handle8(d,token)&&state_block_handle8(d,token)!=old&&state_block_handle8(d,token)->handle8==token&&state_block_handle8(d,token)->refcount==1&&d->refcount==1&&!d->child_parent_refs);
        CHECK(!TOKEN_APPLY(&d->iface,token)&&d->vertex_declaration==b&&!d->vertex_shader);
        if(order==0){CHECK(!TOKEN_DELETE(&d->iface,token));CHECK(TOKEN_DELETE(&d->iface,token)==D3DERR_INVALIDCALL&&!state_block_handle8(d,token));CHECK(!device_release(&d->iface));}
        else if(order==1){CHECK(!device_release(&d->iface));}
        else if(order==2){Interface*view=NULL;CHECK(!device_get_vertex_declaration(&d->iface,&view)&&view==&b->iface);CHECK(device_release(&d->iface)==1);CHECK(!TOKEN_DELETE(&d->iface,token)&&!decl_release(view));}
        else {Interface*normal=NULL;CHECK(!device_begin_state_block(&d->iface)&&!device_end_state_block(&d->iface,&normal));CHECK(((D9StateBlock*)normal)->device_parent_ref&&!((D9StateBlock*)normal)->handle8);CHECK(device_release(&d->iface)==1);CHECK(!TOKEN_APPLY(&d->iface,token)&&!state_block_release(normal));}
        CHECK(!live&&!owner.refs);previous=token;cases++;
    }
    /* All eight retained resource families are private references, including
     * default-pool objects; tokens never introduce Reset parent blockers. */
    for(UINT round=0;round<64;round++){
        Parent owner={0};D9Device*d=new_device(&owner);StateGroup g=new_state_group(d,round%2);DWORD token=0;
        CHECK(!TOKEN_CREATE(&d->iface,token8_group_record,&g,&token));CHECK(!state_group_refs(g,1,1)&&!release_state_group(g));
        CHECK(!state_group_refs(g,0,1)&&d->refcount==1&&!d->child_parent_refs&&!device_has_reset_blockers(d));
        CHECK(!TOKEN_APPLY(&d->iface,token)&&!state_group_refs(g,0,2));
        if(round%2){CHECK(!TOKEN_DELETE(&d->iface,token)&&!state_group_refs(g,0,1));}
        CHECK(!device_release(&d->iface)&&!live&&!owner.refs);cases++;
    }
    Parent owner={0};D9Device*d=new_device(&owner);DWORD token=0x1234,out=0x1234;
    UINT calls=snapshot8_calls,alive=live,allocated=allocations;LONG pins=d->refcount;
#define TOKEN_GUARD(expression) do{CHECK((expression)==D3DERR_INVALIDCALL);CHECK(snapshot8_calls==calls&&allocations==allocated&&live==alive&&d->refcount==pins&&!d->recording_result);cases++;}while(0)
    TOKEN_GUARD(TOKEN_CREATE(NULL,snapshot8_record,NULL,&out));CHECK(!out);
    TOKEN_GUARD(TOKEN_CREATE(&d->iface,NULL,NULL,&out));CHECK(!out);
    TOKEN_GUARD(TOKEN_CREATE(&d->iface,snapshot8_record,NULL,NULL));
    TOKEN_GUARD(TOKEN_APPLY(NULL,1));TOKEN_GUARD(TOKEN_DELETE(NULL,1));TOKEN_GUARD(TOKEN_CAPTURE(NULL,1,snapshot8_record,NULL));
    TOKEN_GUARD(TOKEN_APPLY(&d->iface,0));TOKEN_GUARD(TOKEN_DELETE(&d->iface,0));TOKEN_GUARD(TOKEN_CAPTURE(&d->iface,0,snapshot8_record,NULL));
    TOKEN_GUARD(TOKEN_APPLY(&d->iface,0xabcdef));TOKEN_GUARD(TOKEN_DELETE(&d->iface,0xabcdef));TOKEN_GUARD(TOKEN_CAPTURE(&d->iface,0xabcdef,snapshot8_record,NULL));
    active_compatibility_api=9;TOKEN_GUARD(TOKEN_CREATE(&d->iface,snapshot8_record,NULL,&out));CHECK(!out);active_compatibility_api=8;
#undef TOKEN_GUARD
    fail_allocation=allocations+1;CHECK(TOKEN_CREATE(&d->iface,snapshot8_record,NULL,&out)==E_OUTOFMEMORY&&!out&&snapshot8_calls==calls&&!d->state_blocks&&d->refcount==pins&&live==alive);fail_allocation=0;cases++;
    CHECK(!TOKEN_CREATE(&d->iface,snapshot8_record,NULL,&token)&&token);D9StateBlock*old=state_block_handle8(d,token);alive=live;pins=d->refcount;
    CHECK(TOKEN_CAPTURE(&d->iface,token,NULL,NULL)==D3DERR_INVALIDCALL&&state_block_handle8(d,token)==old);cases++;
    active_compatibility_api=9;CHECK(TOKEN_APPLY(&d->iface,token)==D3DERR_INVALIDCALL&&TOKEN_CAPTURE(&d->iface,token,snapshot8_record,NULL)==D3DERR_INVALIDCALL&&TOKEN_DELETE(&d->iface,token)==D3DERR_INVALIDCALL&&state_block_handle8(d,token)==NULL);active_compatibility_api=8;CHECK(state_block_handle8(d,token)==old);cases++;
    fail_allocation=allocations+1;CHECK(TOKEN_CAPTURE(&d->iface,token,snapshot8_record,NULL)==E_OUTOFMEMORY&&state_block_handle8(d,token)==old&&old->refcount==1&&d->refcount==pins&&live==alive);fail_allocation=0;cases++;
    Snapshot8Context failure={NULL,NULL,0,0,-77};CHECK(TOKEN_CAPTURE(&d->iface,token,snapshot8_record,&failure)==-77&&state_block_handle8(d,token)==old&&old->refcount==1&&!d->recording_result&&live==alive);cases++;
    CHECK(!device_begin_state_block(&d->iface));D9StateBlock*recording=d->recording_result;
    CHECK(TOKEN_CREATE(&d->iface,snapshot8_record,NULL,&out)==D3DERR_INVALIDCALL&&!out&&d->recording_result==recording);cases++;
    CHECK(TOKEN_CAPTURE(&d->iface,token,snapshot8_record,NULL)==D3DERR_INVALIDCALL&&d->recording_result==recording&&state_block_handle8(d,token)==old);cases++;
    CHECK(TOKEN_APPLY(&d->iface,token)==D3DERR_INVALIDCALL&&d->recording_result==recording);cases++;
    device_cancel_recording(d);
    /* Deleting the old token during the recorder must not resurrect it. */
    TokenDeleteContext reentrant={{NULL,NULL,0,0,0},token,123};
    CHECK(TOKEN_CAPTURE(&d->iface,token,token8_delete_record,&reentrant)==D3DERR_INVALIDCALL&&!reentrant.result&&!state_block_handle8(d,token)&&!d->state_blocks&&!d->recording_result&&d->refcount==pins&&live==alive-1);cases++;
    /* Token allocation skips an occupied value even after counter wrap. */
    DWORD many[32];for(UINT i=0;i<32;i++){CHECK(!TOKEN_CREATE(&d->iface,snapshot8_record,NULL,&many[i])&&many[i]);for(UINT j=0;j<i;j++)CHECK(many[i]!=many[j]);}
    handles=many[15]-1;CHECK(!TOKEN_CREATE(&d->iface,snapshot8_record,NULL,&out)&&out!=many[15]&&out>many[31]);
    CHECK(!TOKEN_DELETE(&d->iface,out));for(UINT i=0;i<32;i++)CHECK(!TOKEN_DELETE(&d->iface,many[(i*7)%32]));CHECK(!d->state_blocks&&d->refcount==1);cases++;
    CHECK(!device_release(&d->iface)&&!live&&!owner.refs);
    /* Actual texture private-data callbacks run during token retirement. */
    for(UINT mode=0;mode<3;mode++){
        Parent parent={0};d=new_device(&parent);StateGroup g=new_state_group(d,1);TokenPrivate x={.device=d,.mode=mode};BYTE tag[16]={1};
        CHECK(!d9wg_private_set(&g.texture->private_data,tag,&x,sizeof(void*),D9WG_PRIVATE_IUNKNOWN,&d9wg_private_ops)&&x.refs==1);
        if(mode)CHECK(!TOKEN_CREATE(&d->iface,snapshot8_record,NULL,&x.peer));
        CHECK(!TOKEN_CREATE(&d->iface,token8_group_record,&g,&x.token)&&!release_state_group(g)&&d->refcount==1);
        if(!mode){CHECK(!TOKEN_DELETE(&d->iface,x.token));CHECK(x.calls==1&&!x.refs&&x.pins==2&&x.deleted==D3DERR_INVALIDCALL&&x.applied==D3DERR_INVALIDCALL&&!x.create_result&&x.created&&state_block_handle8(d,x.created));CHECK(!TOKEN_DELETE(&d->iface,x.created)&&!device_release(&d->iface));}
        else {ULONG result=device_release(&d->iface);CHECK(result==(mode==2?1u:0u)&&x.calls==1&&!x.refs&&x.deleted==D3DERR_INVALIDCALL&&x.applied==D3DERR_INVALIDCALL&&x.create_result==D3DERR_INVALIDCALL&&!x.created&&x.peer_apply==D3DERR_INVALIDCALL&&x.peer_delete==D3DERR_INVALIDCALL);if(mode==2){CHECK(!d->state_blocks&&!d->releasing_owned_refs&&!TOKEN_CREATE(&d->iface,snapshot8_record,NULL,&out)&&out);CHECK(!device_release(&d->iface));}}
        CHECK(!live&&!parent.refs);cases++;
    }
    /* Apply releases the previous live texture, whose private-data callback
     * deletes the very block being applied. The operation storage pin keeps
     * its remaining members readable until replay completes. */
    {
        Parent parent={0};d=new_device(&parent);StateGroup a=new_state_group(d,1),b=new_state_group(d,1);TokenPrivate x={.device=d,.mode=3};BYTE tag[16]={2};
        CHECK(!bind_state_group(d,a)&&!d9wg_private_set(&a.texture->private_data,tag,&x,sizeof(void*),D9WG_PRIVATE_IUNKNOWN,&d9wg_private_ops));
        CHECK(!TOKEN_CREATE(&d->iface,token8_group_record,&b,&x.token)&&!release_state_group(a)&&!release_state_group(b));
        CHECK(!TOKEN_APPLY(&d->iface,x.token)&&x.calls==1&&x.block_pins==2&&x.pins==2&&!x.deleted&&!x.create_result&&x.created&&!state_block_handle8(d,x.token));
        CHECK(d->vertex_declaration==b.decl&&d->vertex_shader==b.vs&&!state_group_refs(b,0,1));
        CHECK(!TOKEN_DELETE(&d->iface,x.created)&&!device_release(&d->iface)&&!live&&!parent.refs);cases++;
    }
    {
        Parent parent={0};d=new_device(&parent);TokenPinContext x={0,2,FALSE};
        CHECK(!TOKEN_CREATE(&d->iface,token8_pin_record,&x,&x.token)&&x.token);x.capture=TRUE;
        CHECK(!TOKEN_CAPTURE(&d->iface,x.token,token8_pin_record,&x));
        CHECK(d->refcount==1&&!d->child_parent_refs&&!TOKEN_DELETE(&d->iface,x.token)&&!device_release(&d->iface)&&!live&&!parent.refs);cases++;
    }
    {
        Parent first={0},second={0};D9Device*a=new_device(&first);DWORD token=0;
        CHECK(!TOKEN_CREATE(&a->iface,snapshot8_record,NULL,&token));D9Device*b=new_device(&second);
        CHECK(TOKEN_APPLY(&b->iface,token)==D3DERR_INVALIDCALL&&TOKEN_CAPTURE(&b->iface,token,snapshot8_record,NULL)==D3DERR_INVALIDCALL&&TOKEN_DELETE(&b->iface,token)==D3DERR_INVALIDCALL);
        CHECK(!device_release(&b->iface)&&!second.refs);active_device=a;
        CHECK(state_block_handle8(a,token)&&!TOKEN_APPLY(&a->iface,token)&&!device_release(&a->iface)&&!live&&!first.refs);cases++;
    }
    active_compatibility_api=9;
    printf("PASS actual core8 DWORD state blocks: %u checks / %u cases; no device parent cycle,all8 resource families,stable Capture replacement/OOM/abort,ordinary9 coexistence,invalid/cross-profile/stale tokens,recording guards,collision skip,reentrant delete/no resurrection,actual private-data finalizers anddevice revival,exact cleanup\n",checks-before,cases);
    return 0;
}
