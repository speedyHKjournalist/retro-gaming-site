/* Frozen preadapter decoder isindependent of the productionlinked runtime. */
#include "tests/fixtures/d3d8_fvf_before_core.c"
static int fvf8_core_cases(void)
{
    UINT before_checks=checks,cases=0,valid_cases=0,invalid_cases=0;
    active_compatibility_api=8;
    const DWORD bits[]={0x10,0x20,0x40,0x80,0x1000,0x4000,0x8000};
    for(UINT position=0;position<8;position++)for(UINT flags=0;flags<128;flags++)
    for(UINT textures=0;textures<=9;textures++)for(UINT encoding=0;encoding<4;encoding++) {
        DWORD fvf=position*2u|(textures<<8);
        for(UINT b=0;b<7;b++)if(flags&(1u<<b))fvf|=bits[b];
        for(UINT tex=0;tex<8;tex++)fvf|=((encoding+tex)%4)<<(16+tex*2);
        D9WGVertexElement expected[32];UINT count=999;
        BOOL valid=!fvf||oracle_fvf_decode(8,fvf,expected,32,&count,NULL);
        Parent parent={0};D9Device*d=new_device(&parent);
        D9VertexDeclaration*old=new_declaration(d);D9Shader*program=new_shader(d,FALSE);
        CHECK(!device_set_vertex_declaration(&d->iface,&old->iface)&&!device_set_vertex_shader(&d->iface,&program->iface.vertex));
        CHECK(!decl_release(&old->iface)&&!vertex_shader_release(&program->iface.vertex)&&d->refcount==1);
        UINT creates_before=fvf_creates;
        HRESULT hr=d9wg_d3d9_core_set_fvf8(&d->iface,fvf);
        if(!valid){CHECK(hr==D3DERR_INVALIDCALL&&d->vertex_declaration==old&&d->vertex_shader==program&&fvf_creates==creates_before);invalid_cases++;}
        else {
            CHECK(!hr&&!d->vertex_shader&&!d->fvf&&d->refcount==1&&!d->child_parent_refs);
            if(!fvf)CHECK(!d->vertex_declaration&&!d->vertex_declarations&&fvf_creates==creates_before);
            else {
                D9VertexDeclaration*converted=d->vertex_declaration;
                CHECK(converted&&converted->legacy_fvf8==fvf&&!converted->fvf&&!converted->fvf_converted&&converted->fvf_cache_ref&&!converted->refcount&&converted->binding_refs==2);
                CHECK(converted->element_count==count&&!memcmp(converted->elements,expected,count*sizeof(*expected))&&fvf_creates==creates_before+1);
                Interface*view=NULL;CHECK(!device_get_vertex_declaration(&d->iface,&view)&&view==&converted->iface&&converted->refcount==1&&d->refcount==2);
                D3DVERTEXELEMENT9 actual[32];UINT actual_count=32;
                CHECK(!decl_get_declaration(view,actual,&actual_count)&&actual_count==count+1&&!memcmp(actual,expected,count*sizeof(*expected)));
                D3DVERTEXELEMENT9 end=D3DDECL_END();CHECK(!memcmp(actual+count,&end,sizeof(end))&&!decl_release(view)&&d->refcount==1);
                CHECK(!d9wg_d3d9_core_set_fvf8(&d->iface,fvf)&&d->vertex_declaration==converted&&converted->binding_refs==2&&fvf_creates==creates_before+1);
                /* The API9 zero control isstill a no-op on the same object. */
                CHECK(!device_set_fvf(&d->iface,0)&&d->vertex_declaration==converted);
                CHECK(!d9wg_d3d9_core_set_fvf8(&d->iface,0)&&!d->vertex_declaration&&!d->vertex_shader&&converted->binding_refs==1);
                CHECK(!d9wg_d3d9_core_set_fvf8(&d->iface,fvf)&&d->vertex_declaration==converted&&converted->binding_refs==2);
                /* Both dialects cancachethe same rawnumber withoutaliasing. */
                D9WGVertexElement ordinary[18];UINT ordinary_count;
                if(oracle_fvf_decode(9,fvf,ordinary,18,&ordinary_count,NULL)) {
                    CHECK(!device_set_fvf(&d->iface,fvf)&&d->vertex_declaration!=converted&&d->vertex_declaration->fvf_converted&&!d->vertex_declaration->legacy_fvf8);
                    CHECK(!memcmp(d->vertex_declaration->elements,ordinary,ordinary_count*sizeof(*ordinary)));
                    CHECK(!d9wg_d3d9_core_set_fvf8(&d->iface,fvf)&&d->vertex_declaration==converted&&!d->fvf);
                }
                /* Realstate-block methods retain the converted elements with
                 * fvfassociationzero; never reinterpret the8 key as9. */
                if((cases&31u)==0) {
                    CHECK(!device_begin_state_block(&d->iface));
                    CHECK(!d9wg_d3d9_core_set_fvf8(&d->iface,0)&&d->vertex_declaration==converted);
                    CHECK(d->recording_result->has_vertex_format&&d->recording_result->has_vertex_shader&&!d->recording_result->vertex_declaration&&!d->recording_result->vertex_shader&&!d->recording_result->fvf);
                    Interface*zero=NULL;CHECK(!device_end_state_block(&d->iface,&zero));
                    CHECK(!state_block_apply_method(zero)&&!d->vertex_declaration&&!d->vertex_shader&&!d->fvf);
                    CHECK(!state_block_release(zero)&&!d9wg_d3d9_core_set_fvf8(&d->iface,fvf));
                    CHECK(!device_begin_state_block(&d->iface)&&!d9wg_d3d9_core_set_fvf8(&d->iface,fvf));
                    Interface*block=NULL;CHECK(!device_end_state_block(&d->iface,&block));
                    CHECK(((D9StateBlock*)block)->has_vertex_format&&((D9StateBlock*)block)->vertex_declaration==converted&&!((D9StateBlock*)block)->fvf);
                    device_clear_bindings(d);device_clear_fvf_cache(d);
                    CHECK(!converted->fvf_cache_ref&&converted->binding_refs==1);
                    CHECK(!state_block_apply_method(block)&&d->vertex_declaration==converted&&!d->fvf);
                    CHECK(!d9wg_d3d9_core_set_fvf8(&d->iface,fvf)&&converted->fvf_cache_ref&&d->vertex_declaration==converted&&converted->binding_refs==3);
                    CHECK(!state_block_release(block)&&converted->binding_refs==2);
                }
            }
            valid_cases++;
        }
        CHECK(!device_release(&d->iface)&&!live&&!parent.refs);cases++;
    }
    /* Directzero on liveprogrammable state,pluslast-public-view ownership
     * withboth cache-first anddevice-first teardown. */
    for(UINT order=0;order<4;order++) {
        Parent owner={0};D9Device*device=new_device(&owner);
        D9VertexDeclaration*original=new_declaration(device);D9Shader*vs=new_shader(device,FALSE);
        CHECK(!device_set_vertex_declaration(&device->iface,&original->iface)&&!device_set_vertex_shader(&device->iface,&vs->iface.vertex));
        CHECK(!decl_release(&original->iface)&&!vertex_shader_release(&vs->iface.vertex));
        CHECK(!d9wg_d3d9_core_set_fvf8(&device->iface,0)&&!device->vertex_shader&&!device->vertex_declaration&&!device->vertex_declarations&&!device->shaders&&device->refcount==1);
        CHECK(!d9wg_d3d9_core_set_fvf8(&device->iface,0)&&device->refcount==1);
        CHECK(!d9wg_d3d9_core_set_fvf8(&device->iface,0x4042));Interface*held=NULL;
        CHECK(!device_get_vertex_declaration(&device->iface,&held)&&held&&device->refcount==2);
        D9VertexDeclaration*entry=(D9VertexDeclaration*)held;
        CHECK(entry->legacy_fvf8==0x4042&&entry->elements[0].Type==2&&!entry->fvf&&entry->refcount==1);
        if(order==0){device_clear_bindings(device);device_clear_fvf_cache(device);CHECK(entry->refcount==1&&!entry->binding_refs&&!entry->fvf_cache_ref);CHECK(!d9wg_d3d9_core_set_fvf8(&device->iface,0x4042)&&device->vertex_declaration==entry&&entry->fvf_cache_ref&&entry->binding_refs==2);CHECK(!decl_release(held)&&!device_release(&device->iface));}
        else if(order==1){CHECK(device_release(&device->iface)==1&&!decl_release(held));}
        else if(order==2){device_clear_bindings(device);device_clear_fvf_cache(device);CHECK(!decl_release(held)&&!device_release(&device->iface));}
        else {CHECK(!decl_release(held)&&!device_release(&device->iface));}
        CHECK(!live&&!owner.refs);cases++;
    }
    /* Profile, null andinvalid inputs are rejected beforeallocation/state. */
    Parent parent={0};D9Device*d=new_device(&parent);UINT allocations_before=allocations;
    CHECK(d9wg_d3d9_core_set_fvf8(NULL,2)==D3DERR_INVALIDCALL);
    active_compatibility_api=9;CHECK(d9wg_d3d9_core_set_fvf8(&d->iface,2)==D3DERR_INVALIDCALL&&allocations==allocations_before);
    active_compatibility_api=8;CHECK(d9wg_d3d9_core_set_fvf8(&d->iface,1)==D3DERR_INVALIDCALL&&allocations==allocations_before);
    CHECK(d9wg_d3d9_core_set_fvf8(&d->iface,0xffffffffu)==D3DERR_INVALIDCALL&&allocations==allocations_before);
    fail_allocation=allocations+1;CHECK(d9wg_d3d9_core_set_fvf8(&d->iface,2)==E_OUTOFMEMORY&&!d->vertex_declarations&&!d->vertex_declaration&&d->refcount==1);fail_allocation=0;
    fail_emit=1;CHECK(d9wg_d3d9_core_set_fvf8(&d->iface,2)==D3DERR_DRIVERINTERNALERROR&&!d->vertex_declarations&&!d->vertex_declaration&&d->refcount==1);fail_emit=0;
    CHECK(!device_release(&d->iface)&&!live&&!parent.refs);cases+=6;
    active_compatibility_api=9;
    printf("PASS actual core8 FVF bindings: %u checks / %u cases (%u accepted/%u refused); frozen8/9 decoder,versioned layouts,zero clears VS/layout,real9 cachepublic-zero/identity/parentpins,state-blockrecord/apply/cache-reacquire,ordinary9 no-op/cache separation andexact cleanup\n",checks-before_checks,cases,valid_cases,invalid_cases);
    return 0;
}
