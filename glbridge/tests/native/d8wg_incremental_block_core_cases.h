#define BLOCK_BEGIN8 d9wg_d3d9_core_begin_state_block8
#define BLOCK_END8 d9wg_d3d9_core_end_state_block_handle8
static int incremental8_block_cases(void)
{
    UINT before=checks,cases=0;active_compatibility_api=8;
    for(UINT round=0;round<128;round++)for(UINT order=0;order<4;order++)for(UINT pool=0;pool<2;pool++){
        Parent parent={0};D9Device*d=new_device(&parent);StateGroup a=new_state_group(d,pool),b=new_state_group(d,pool);DWORD token=0x1234;
        CHECK(!bind_state_group(d,a));LONG pins=d->refcount,children=d->child_parent_refs;UINT sent=pair_submissions;
        CHECK(!BLOCK_BEGIN8(&d->iface)&&d->recording_result&&d->recording_result->refcount==1&&!d->recording_result->device_parent_ref&&!d->recording_result->handle8);
        D9StateBlock*recording=d->recording_result;
        CHECK(BLOCK_BEGIN8(&d->iface)==D3DERR_INVALIDCALL&&d->recording_result==recording);
        CHECK(!bind_state_group(d,a)&&!bind_state_group(d,b)&&!bind_state_group(d,b));
        CHECK(d->refcount==pins&&d->child_parent_refs==children&&pair_submissions==sent&&d->vertex_declaration==a.decl&&d->vertex_shader==a.vs&&d->streams[0].buffer==a.vb&&d->textures[0]==a.texture);
        CHECK(!state_group_refs(a,1,1)&&!state_group_refs(b,1,1));
        CHECK(BLOCK_END8(&d->iface,NULL)==D3DERR_INVALIDCALL&&d->recording_result==recording);
        CHECK(!BLOCK_END8(&d->iface,&token)&&token&&!d->recording_result&&state_block_handle8(d,token)==recording);
        CHECK(recording->refcount==1&&!recording->device_parent_ref&&d->refcount==pins&&d->child_parent_refs==children&&pair_submissions==sent);
        CHECK(!release_state_group(a)&&!release_state_group(b)&&d->refcount==1&&!d->child_parent_refs&&!device_has_reset_blockers(d));
        CHECK(!TOKEN_APPLY(&d->iface,token)&&!state_group_refs(b,0,2));
        if(order==0){CHECK(!TOKEN_DELETE(&d->iface,token)&&!state_group_refs(b,0,1)&&!device_release(&d->iface));}
        else if(order==1){CHECK(!device_release(&d->iface));}
        else if(order==2){Interface*view=NULL;CHECK(!device_get_vertex_declaration(&d->iface,&view)&&view==&b.decl->iface);CHECK(device_release(&d->iface)==1&&!TOKEN_DELETE(&d->iface,token)&&!decl_release(view));}
        else {CHECK(!BLOCK_BEGIN8(&d->iface));DWORD empty=0;CHECK(!BLOCK_END8(&d->iface,&empty)&&empty&&empty!=token&&d->refcount==1&&!TOKEN_DELETE(&d->iface,token)&&!device_release(&d->iface));}
        CHECK(!live&&!parent.refs);cases++;
    }
    Parent owner={0};D9Device*d=new_device(&owner);DWORD out=0x1234;UINT alive=live,allocated=allocations;
    CHECK(BLOCK_BEGIN8(NULL)==D3DERR_INVALIDCALL&&BLOCK_END8(NULL,&out)==D3DERR_INVALIDCALL&&!out&&BLOCK_END8(NULL,NULL)==D3DERR_INVALIDCALL&&live==alive&&allocations==allocated);cases++;
    out=0x1234;CHECK(BLOCK_END8(&d->iface,&out)==D3DERR_INVALIDCALL&&!out&&!d->recording_result&&d->refcount==1&&!d->child_parent_refs&&live==alive&&allocations==allocated);cases++;
    active_compatibility_api=9;out=0x1234;CHECK(BLOCK_BEGIN8(&d->iface)==D3DERR_INVALIDCALL&&BLOCK_END8(&d->iface,&out)==D3DERR_INVALIDCALL&&!out&&!d->recording_result&&live==alive&&allocations==allocated);active_compatibility_api=8;cases++;
    d->releasing_owned_refs=TRUE;CHECK(BLOCK_BEGIN8(&d->iface)==D3DERR_INVALIDCALL&&!d->recording_result&&allocations==allocated&&live==alive&&d->refcount==1);d->releasing_owned_refs=FALSE;cases++;
    fail_allocation=allocations+1;CHECK(BLOCK_BEGIN8(&d->iface)==E_OUTOFMEMORY&&!d->recording_result&&!d->state_blocks&&d->refcount==1&&live==alive);fail_allocation=0;cases++;
    CHECK(!BLOCK_BEGIN8(&d->iface));D9StateBlock*recording=d->recording_result;LONG pins=d->refcount;alive=live;allocated=allocations;
    active_compatibility_api=9;out=0x1234;CHECK(BLOCK_END8(&d->iface,&out)==D3DERR_INVALIDCALL&&!out&&d->recording_result==recording&&d->refcount==pins&&allocations==allocated&&live==alive);active_compatibility_api=8;cases++;
    d->releasing_owned_refs=TRUE;out=0x1234;CHECK(BLOCK_BEGIN8(&d->iface)==D3DERR_INVALIDCALL&&BLOCK_END8(&d->iface,&out)==D3DERR_INVALIDCALL&&!out&&d->recording_result==recording&&d->refcount==pins&&live==alive);d->releasing_owned_refs=FALSE;cases++;
    out=0x1234;CHECK(TOKEN_CREATE(&d->iface,snapshot8_record,NULL,&out)==D3DERR_INVALIDCALL&&!out&&d->recording_result==recording);cases++;
    CHECK(!BLOCK_END8(&d->iface,&out)&&out&&state_block_handle8(d,out)==recording&&!TOKEN_DELETE(&d->iface,out));cases++;
    out=0x1234;CHECK(BLOCK_END8(&d->iface,&out)==D3DERR_INVALIDCALL&&!out&&!d->recording_result&&d->refcount==1);cases++;
    /* The recorder remains actual9 storage: internal9 Begin/End can coexist
     * while published9 COM blocks retain their ordinary parent semantics. */
    CHECK(!device_begin_state_block(&d->iface)&&!BLOCK_END8(&d->iface,&out)&&out&&!TOKEN_DELETE(&d->iface,out));cases++;
    Interface*normal=NULL;CHECK(!BLOCK_BEGIN8(&d->iface)&&!device_end_state_block(&d->iface,&normal)&&normal&&((D9StateBlock*)normal)->device_parent_ref&&!((D9StateBlock*)normal)->handle8&&d->refcount==2&&d->child_parent_refs==1);CHECK(!state_block_release(normal)&&d->refcount==1&&!device_release(&d->iface)&&!live&&!owner.refs);cases++;
    for(UINT pool=0;pool<2;pool++){
        Parent p={0};d=new_device(&p);StateGroup group=new_state_group(d,pool);UINT retired=destroys;
        CHECK(!BLOCK_BEGIN8(&d->iface)&&!bind_state_group(d,group)&&!release_state_group(group)&&d->refcount==1&&!d->child_parent_refs);
        CHECK(!device_release(&d->iface)&&!live&&!p.refs&&destroys==retired+8);cases++;
    }
    active_compatibility_api=9;
    printf("PASS actual core8 incremental state blocks: %u checks / %u cases; actual9 Begin/End todevice-owned DWORD,all8 resource families,duplicate recording/live isolation,public-zero/fourcleanuporders,invalid End preservesrecording,profile/teardown/OOM guards,ordinary9 interop andabandonedrecording cleanup\n",checks-before,cases);
    return 0;
}
