#define VIEW_GET d9wg_d3d9_core_get_view8
#define VIEW_ATTACH d9wg_d3d9_core_attach_view8
typedef struct View8Context {IUnknown*object;UINT calls;TokenPrivate*private_data;D9VertexBuffer*release_other;}View8Context;
static UINT view8_errors;
static void WINAPI view8_retired(void*data)
{
    View8Context*x=data;x->calls++;
    if(view_lock_depth||x->calls!=1)view8_errors++;
    if(x->private_data&&(x->private_data->calls!=1||x->private_data->refs))view8_errors++;
    void*got=(void*)0x11;
    if(VIEW_GET(x->object,&got)!=D3DERR_INVALIDCALL||got)view8_errors++;
    if(VIEW_ATTACH(x->object,x,view8_retired)!=D3DERR_INVALIDCALL)view8_errors++;
    core_view_retire8(x->object); /* recursive retirement is idempotent */
    if(x->release_other)vb_release(&x->release_other->iface);
}
static int view8_attach_one(Interface*object,View8Context*x)
{
    void*found=(void*)0x11;x->object=object;UINT allocated=allocations;
    CHECK(!VIEW_GET(object,&found)&&!found&&allocations==allocated);
    CHECK(!VIEW_ATTACH(object,x,view8_retired));allocated=allocations;
    CHECK(!VIEW_GET(object,&found)&&found==x&&allocations==allocated&&!x->calls);
    CHECK(VIEW_ATTACH(object,(void*)0x22,view8_retired)==D3DERR_INVALIDCALL&&allocations==allocated&&!VIEW_GET(object,&found)&&found==x);
    return 0;
}
static int view8_core_cases(void)
{
    UINT before=checks,cases=0;active_compatibility_api=8;
    for(UINT round=0;round<128;round++)for(UINT order=0;order<4;order++)for(UINT pool=0;pool<2;pool++){
        Parent parent={0};D9Device*d=new_device(&parent);StateGroup g=new_state_group(d,pool);View8Context views[10]={0};Interface*surfaces[3]={0};DWORD token=0;
        CHECK(!TOKEN_CREATE(&d->iface,token8_group_record,&g,&token));
        TokenPrivate private_data={.device=d,.mode=1};BYTE tag[16]={3};
        CHECK(!d9wg_private_set(&g.texture->private_data,tag,&private_data,sizeof(void*),D9WG_PRIVATE_IUNKNOWN,&d9wg_private_ops));views[0].private_data=&private_data;
        Interface*objects[5]={&g.texture->iface,&g.cube->iface,&g.volume->iface,&g.vb->iface,&g.ib->iface};
        LONG pins=d->refcount,children=d->child_parent_refs;
        for(UINT i=0;i<5;i++)CHECK(!view8_attach_one(objects[i],&views[i]));
        CHECK(d->refcount==pins&&d->child_parent_refs==children&&!state_group_refs(g,1,1));
        CHECK(!texture_get_surface_level(&g.texture->iface,0,&surfaces[0]));
        CHECK(!cube_get_cube_map_surface(&g.cube->iface,0,0,&surfaces[1]));
        /* The retained metadata factory predates volume-vtable checks. */
        for(UINT i=0;i<g.volume->level_count;i++)g.volume->levels[i].level_volume->iface.lpVtbl=&g_volume_vtbl;
        CHECK(!volume_texture_get_volume_level(&g.volume->iface,0,&surfaces[2]));
        for(UINT i=0;i<3;i++)CHECK(!view8_attach_one(surfaces[i],&views[i+5]));
        CHECK(surface_release(surfaces[0])==1&&surface_release(surfaces[1])==1&&volume_release(surfaces[2])==1);
        D9SwapChain implicit={0};D9Surface*bb=HeapAlloc(NULL,8,sizeof(*bb)),*depth=HeapAlloc(NULL,8,sizeof(*depth));
        bb->iface.lpVtbl=&g_surface_vtbl;bb->device=d;bb->backbuffer=TRUE;bb->swap_chain=&implicit;d->implicit_back_buffer=bb;
        depth->iface.lpVtbl=&g_surface_vtbl;depth->device=d;depth->auto_depth_stencil=TRUE;d->implicit_depth_stencil=depth;
        CHECK(surface_add_ref(&bb->iface)==1&&surface_add_ref(&depth->iface)==1);
        CHECK(!view8_attach_one(&bb->iface,&views[8])&&!view8_attach_one(&depth->iface,&views[9]));
        CHECK(!surface_release(&bb->iface)&&!surface_release(&depth->iface));
        CHECK(!release_state_group(g)&&d->refcount==1&&!d->child_parent_refs);
        for(UINT i=0;i<10;i++){void*found=NULL;CHECK(!VIEW_GET(views[i].object,&found)&&found==&views[i]&&!views[i].calls);}
        CHECK(!TOKEN_APPLY(&d->iface,token)&&!state_group_refs(g,0,2));
        if(order==0){CHECK(!TOKEN_DELETE(&d->iface,token)&&!device_release(&d->iface));}
        else if(order==1){CHECK(!device_release(&d->iface));}
        else if(order==2){Interface*view=NULL;CHECK(!device_get_texture(&d->iface,0,&view)&&view==&g.texture->iface);CHECK(!TOKEN_DELETE(&d->iface,token)&&device_release(&d->iface)==1&&!texture_release(view));}
        else {device_clear_bindings(d);CHECK(!TOKEN_DELETE(&d->iface,token));CHECK(!device_release(&d->iface));}
        for(UINT i=0;i<10;i++)CHECK(views[i].calls==1);
        CHECK(!g_core_views8&&!view8_errors&&!view_lock_depth&&!live&&!parent.refs);cases++;
    }
    Parent owner={0};D9Device*d=new_device(&owner);D9VertexBuffer*b=new_buffer(d,1,FALSE);View8Context x={.object=&b->iface};void*out=(void*)0x11;UINT allocated=allocations,alive=live;LONG pins=d->refcount;
#define VIEW_GUARD(expression) do{CHECK((expression)==D3DERR_INVALIDCALL);CHECK(allocations==allocated&&live==alive&&d->refcount==pins&&!g_core_views8&&!view_lock_depth);cases++;}while(0)
    VIEW_GUARD(VIEW_GET(NULL,&out));CHECK(!out);VIEW_GUARD(VIEW_GET(&b->iface,NULL));
    VIEW_GUARD(VIEW_ATTACH(NULL,&x,view8_retired));VIEW_GUARD(VIEW_ATTACH(&b->iface,NULL,view8_retired));VIEW_GUARD(VIEW_ATTACH(&b->iface,&x,NULL));
    Interface wrong={.lpVtbl=(void*)0x11};VIEW_GUARD(VIEW_GET(&wrong,&out));CHECK(!out);VIEW_GUARD(VIEW_ATTACH(&wrong,&x,view8_retired));
    active_compatibility_api=9;out=(void*)0x11;VIEW_GUARD(VIEW_GET(&b->iface,&out));CHECK(!out);VIEW_GUARD(VIEW_ATTACH(&b->iface,&x,view8_retired));active_compatibility_api=8;
#undef VIEW_GUARD
    fail_allocation=allocations+1;CHECK(VIEW_ATTACH(&b->iface,&x,view8_retired)==E_OUTOFMEMORY&&!g_core_views8&&live==alive&&d->refcount==pins&&!view_lock_depth);fail_allocation=0;cases++;
    CHECK(!view8_attach_one(&b->iface,&x)&&!vb_release(&b->iface)&&x.calls==1&&!g_core_views8&&!device_release(&d->iface)&&!live&&!owner.refs);cases++;
    /* Retiring a tail entry may release the head object from its callback.
     * The registry must find the link again after that nested destruction. */
    {
        Parent p={0};d=new_device(&p);D9VertexBuffer*a=new_buffer(d,1,FALSE),*b=new_buffer(d,1,FALSE);View8Context first={0},second={.release_other=a};
        CHECK(!view8_attach_one(&a->iface,&first)&&!view8_attach_one(&b->iface,&second));
        CHECK(!vb_release(&b->iface)&&first.calls==1&&second.calls==1&&!g_core_views8&&!view8_errors&&!device_release(&d->iface)&&!live&&!p.refs);cases++;
    }
    /* Additional-chain recognition executes the actual vtable switch. Its
     * actual native COM destruction is covered by the separate chain harness
     * and the real SDK/XP probe;this metadata-only fixture has no chain backend. */
    {
        Parent p={0};d=new_device(&p);D9SwapChain chain={.iface={&g_swap_chain_vtbl},.device=d,.handle=99};View8Context v={0};
        CHECK(!view8_attach_one(&chain.iface,&v));core_view_retire8(&chain);
        CHECK(v.calls==1&&!g_core_views8&&!view8_errors);chain.handle=0;out=(void*)0x11;
        CHECK(VIEW_GET(&chain.iface,&out)==D3DERR_INVALIDCALL&&!out&&VIEW_ATTACH(&chain.iface,&v,view8_retired)==D3DERR_INVALIDCALL);
        D9VertexDeclaration*decl=new_declaration(d);D9Shader*shader=new_shader(d,FALSE);
        CHECK(VIEW_GET(&decl->iface,&out)==D3DERR_INVALIDCALL&&VIEW_ATTACH(&shader->iface.vertex,&v,view8_retired)==D3DERR_INVALIDCALL);
        CHECK(!decl_release(&decl->iface)&&!vertex_shader_release(&shader->iface.vertex)&&!device_release(&d->iface)&&!live&&!p.refs);cases++;
    }
    {
        Parent p={0};d=new_device(&p);Interface*surface=NULL;View8Context v={0};
        CHECK(!create_target_texture(d,32,24,22,1,0,0,&surface)&&surface);
        TokenPrivate finalizer={.device=d,.mode=1};BYTE tag[16]={4};
        CHECK(!d9wg_private_set(&((D9Surface*)surface)->private_data,tag,&finalizer,sizeof(void*),D9WG_PRIVATE_IUNKNOWN,&d9wg_private_ops));v.private_data=&finalizer;
        CHECK(!view8_attach_one(surface,&v)&&!surface_release(surface)&&v.calls==1&&!g_core_views8&&!view8_errors&&!device_release(&d->iface)&&!live&&!p.refs);cases++;
    }
    active_compatibility_api=9;
    printf("PASS actual core8 weak object views: %u checks / %u cases; perstorage identity/public-zero/cache revival,no object/device pins,5 resource parents/3levelviews/implicit surfaces,private-data-before-retire,duplicate/OOM/profile guards,tombstone recursion/nested head retirement,additionalchain type recognition andexact cleanup\n",checks-before,cases);
    return 0;
}
