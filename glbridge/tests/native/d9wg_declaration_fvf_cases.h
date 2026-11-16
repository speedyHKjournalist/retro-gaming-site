/* Independent Windows POSITION-only association cases. Nonzero stream/offset,
 * nondefault method and additional elements retain the previous zero policy;
 * they are conservative guards, not claims about an untested native HAL. */
static const struct {D3DVERTEXELEMENT9 element;DWORD fvf;} declaration_cases[] = {
    {{0,0,2,0,0,0},2},{{0,0,3,0,9,0},4},
    {{0,0,0,0,1,0},0},{{0,0,1,0,1,0},0},{{0,0,2,0,1,0},0},{{0,0,3,0,1,0},0},
    {{0,0,5,0,2,0},0},{{0,0,2,0,3,0},0},{{0,0,0,0,4,0},0},
    {{0,0,4,0,10,0},0},{{0,0,4,0,10,1},0},{{0,0,2,0,0,1},0},{{0,0,2,0,3,1},0},
    {{0,0,0,0,8,0},0},{{0,0,3,0,12,0},0},{{0,0,0,0,13,0},0},
    {{0,0,0,0,5,0},0},{{0,0,1,0,5,0},0},{{0,0,2,0,5,0},0},{{0,0,3,0,5,0},0},
};
static int declaration_view(D9Device*d,Interface*wanted,DWORD fvf)
{
    DWORD out=0xdeadbeef;Interface*view=NULL;
    CHECK(!device_get_fvf(&d->iface,&out)&&out==fvf);
    CHECK(!device_get_vertex_declaration(&d->iface,&view)&&view==wanted);
    CHECK(decl_release(view)>=1);return 0;
}
static int declaration_fvf_cases(void)
{
    UINT old_checks=checks,profiles=0,pairs=0;
    for(UINT round=0;round<32;round++)for(UINT order=0;order<4;order++){
        Parent parent={0};D9Device*d=new_device(&parent);UINT old_creates=fvf_creates,old_destroys=destroys;
        for (UINT offset = 1; offset <= 3; ++offset) {
            D3DVERTEXELEMENT9 bad[2]={{0,offset,4,0,10,0},D3DDECL_END()};
            Interface *out=(void *)(uintptr_t)99;UINT allocated=allocations;
            CHECK(device_create_vertex_declaration(&d->iface,bad,&out)==E_FAIL && !out && allocations==allocated);
            bad[0].Offset=16;bad[0].Type=D3DDECLTYPE_UNUSED;out=(void *)(uintptr_t)99;
            CHECK(device_create_vertex_declaration(&d->iface,bad,&out)==E_FAIL && !out && allocations==allocated);
        }
        for(UINT i=0;i<sizeof(declaration_cases)/sizeof(*declaration_cases);i++){
            D3DVERTEXELEMENT9 elements[2]={declaration_cases[i].element,D3DDECL_END()};Interface*iface=NULL;
            CHECK(!device_create_vertex_declaration(&d->iface,elements,&iface)&&iface);
            D9VertexDeclaration*u=(void*)iface;CHECK(u->fvf==declaration_cases[i].fvf&&!u->fvf_converted&&!u->fvf_cache_ref&&u->refcount==1&&!u->binding_refs&&d->refcount==2);
            CHECK(!device_set_vertex_declaration(&d->iface,iface)&&!declaration_view(d,iface,declaration_cases[i].fvf));
            CHECK(!device_set_vertex_declaration(&d->iface,NULL)&&!decl_release(iface)&&d->refcount==1);
        }
        D3DVERTEXELEMENT9 pos[3]={{0,0,2,0,0,0},D3DDECL_END(),D3DDECL_END()};
        CHECK(!declaration_fvf(NULL,0)&&!declaration_fvf(pos,0));
        for(UINT field=0;field<4;field++){D3DVERTEXELEMENT9 guard=pos[0];if(field==0)guard.Stream=1;if(field==1)guard.Offset=4;if(field==2)guard.Method=1;if(field==3)guard.UsageIndex=1;CHECK(!declaration_fvf(&guard,1));}
        pos[1]=(D3DVERTEXELEMENT9){0,12,4,0,10,0};CHECK(!declaration_fvf(pos,2));pos[1]=D3DDECL_END();
        Interface*xyz=NULL,*rhw=NULL;CHECK(!device_create_vertex_declaration(&d->iface,pos,&xyz));
        CHECK(!device_set_vertex_declaration(&d->iface,xyz)&&!declaration_view(d,xyz,2));
        CHECK(!device_set_fvf(&d->iface,0)&&!declaration_view(d,xyz,2));
        CHECK(!device_set_fvf(&d->iface,2));D9VertexDeclaration*cache_xyz=d->vertex_declaration;
        CHECK(cache_xyz!=(void*)xyz&&cache_xyz->fvf_converted&&cache_xyz->fvf_cache_ref&&!cache_xyz->refcount&&cache_xyz->binding_refs==2);
        CHECK(!device_set_fvf(&d->iface,4));D9VertexDeclaration*cache_rhw=d->vertex_declaration;
        pos[0]=(D3DVERTEXELEMENT9){0,0,3,0,9,0};CHECK(!device_create_vertex_declaration(&d->iface,pos,&rhw));
        for(UINT i=0;i<32;i++){
            CHECK(!device_set_vertex_declaration(&d->iface,xyz)&&!declaration_view(d,xyz,2));
            CHECK(!device_set_fvf(&d->iface,2)&&d->vertex_declaration==cache_xyz);
            CHECK(!device_set_vertex_declaration(&d->iface,rhw)&&!declaration_view(d,rhw,4));
            CHECK(!device_set_fvf(&d->iface,4)&&d->vertex_declaration==cache_rhw);pairs+=2;
        }
        CHECK(!device_begin_state_block(&d->iface)&&!device_set_vertex_declaration(&d->iface,rhw));Interface*block=NULL;
        CHECK(!device_end_state_block(&d->iface,&block)&&!decl_release(rhw));
        CHECK(!state_block_apply_method(block)&&d->vertex_declaration==(void*)rhw&&d->fvf==4);
        CHECK(!state_block_capture_method(block)&&!device_set_fvf(&d->iface,4)&&d->vertex_declaration==cache_rhw);
        CHECK(!state_block_apply_method(block)&&d->vertex_declaration==(void*)rhw&&d->fvf==4);
        CHECK(!device_set_fvf(&d->iface,4)&&!state_block_release(block));
        CHECK(!decl_release(xyz));
        CHECK(fvf_creates==old_creates+24&&destroys==old_destroys+22&&d->refcount==1&&!d->child_parent_refs);
        if(order==0){device_clear_bindings(d);device_clear_fvf_cache(d);CHECK(!d->vertex_declarations&&!device_release(&d->iface));}
        else if(order==1){CHECK(!device_release(&d->iface));}
        else {Interface*held=NULL;CHECK(!device_get_vertex_declaration(&d->iface,&held));
            if(order==2){device_clear_bindings(d);device_clear_fvf_cache(d);CHECK(((D9VertexDeclaration*)held)->fvf_converted&&!((D9VertexDeclaration*)held)->fvf_cache_ref);CHECK(!device_set_fvf(&d->iface,4)&&d->vertex_declaration==(void*)held);CHECK(!decl_release(held)&&!device_release(&d->iface));}
            else {CHECK(device_release(&d->iface)==1&&!decl_release(held));}}
        CHECK(!live&&!parent.refs&&destroys==old_destroys+24);profiles++;
    }
    printf("PASS actual D3D9 user declaration FVF: %u checks, %u owner profiles, %u collision pairs; twenty positive/negative layouts, conservative guards, both cache creation orders, stateblock private lifetime and exact final cleanup\n",checks-old_checks,profiles,pairs);return 0;
}
