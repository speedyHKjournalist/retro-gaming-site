static const struct FVFCase {
    DWORD fvf;UINT count;D3DVERTEXELEMENT9 elements[4];
} fvf_cases[] = {
    {2,1,{{0,0,2,0,0,0}}},
    {4,1,{{0,0,3,0,9,0}}},
    {16,1,{{0,0,2,0,3,0}}},
    {68,2,{{0,0,3,0,9,0},{0,16,4,0,10,0}}},
    {0x112,3,{{0,0,2,0,0,0},{0,12,2,0,3,0},{0,24,1,0,5,0}}},
    {0x1c4,4,{{0,0,3,0,9,0},{0,16,4,0,10,0},{0,20,4,0,10,1},{0,24,1,0,5,0}}},
};
static int fvf_metadata(Interface *iface, UINT index)
{
    D3DVERTEXELEMENT9 elements[18];UINT count = 0;
    CHECK(!decl_get_declaration(iface,NULL,&count) && count == fvf_cases[index].count + 1);
    CHECK(!decl_get_declaration(iface,elements,&count) && count == fvf_cases[index].count + 1);
    CHECK(!memcmp(elements,fvf_cases[index].elements,fvf_cases[index].count * sizeof(*elements)));
    D3DVERTEXELEMENT9 end = D3DDECL_END();CHECK(!memcmp(&elements[count-1],&end,sizeof(end)));return 0;
}
static int fvf_binding_cases(void)
{
    UINT old_checks = checks, profiles = 0, revivals = 0, switches = 0;
    for (UINT round = 0; round < 32; round++) for (UINT order = 0; order < 4; order++) {
        Parent parent = {0};D9Device *device = new_device(&parent);D9VertexDeclaration *views[6] = {0};
        UINT old_creates = fvf_creates, old_destroys = destroys;
        for (UINT cycle = 0; cycle < 64; cycle++) {
            UINT index = (round + cycle) % 6;DWORD key = fvf_cases[index].fvf, value = 0;
            CHECK(!device_set_fvf(&device->iface,key) && device->fvf == key && !device_get_fvf(&device->iface,&value) && value == key);
            D9VertexDeclaration *decl = device->vertex_declaration;
            CHECK(decl && decl->fvf == key && decl->fvf_cache_ref && !decl->refcount && decl->binding_refs == 2 && device->refcount == 1 && !device->child_parent_refs);
            if (views[index]) CHECK(views[index] == decl);else views[index] = decl;
            Interface *first = NULL,*second = NULL;CHECK(!device_get_vertex_declaration(&device->iface,&first) && first == &decl->iface && decl->refcount == 1 && device->refcount == 2 && device->child_parent_refs == 1);
            CHECK(!device_get_vertex_declaration(&device->iface,&second) && second == first && decl->refcount == 2 && device->refcount == 2);
            CHECK(!fvf_metadata(first,index));
            CHECK(decl_release(first) == 1 && !decl_release(second) && device->refcount == 1 && !device->child_parent_refs);
            UINT sets = fvf_sets;CHECK(!device_set_fvf(&device->iface,0) && device->vertex_declaration == decl && device->fvf == key && fvf_sets == sets);
            void *unknown = NULL;CHECK(!decl_query_interface(&decl->iface,&IID_IUnknown,&unknown) && unknown == &decl->iface && device->refcount == 2);
            CHECK(!decl_release(unknown) && device->refcount == 1);
            switches++;revivals += 2;
        }
        CHECK(fvf_creates == old_creates + 6 && destroys == old_destroys);
        D9VertexDeclaration *old_decl = device->vertex_declaration;DWORD old_fvf = device->fvf;
        fail_allocation = allocations + 1;CHECK(device_set_fvf(&device->iface,0x80) == E_OUTOFMEMORY && device->vertex_declaration == old_decl && device->fvf == old_fvf && fvf_creates == old_creates + 6);fail_allocation = 0;
        fail_emit = 1;CHECK(device_set_fvf(&device->iface,0x80) == D3DERR_DRIVERINTERNALERROR && device->vertex_declaration == old_decl && device->fvf == old_fvf && fvf_creates == old_creates + 6);fail_emit = 0;
        CHECK(device_set_fvf(&device->iface,0xffffffff) == D3DERR_INVALIDCALL && device->vertex_declaration == old_decl && device->fvf == old_fvf);
        CHECK(!device_begin_state_block(&device->iface));D9StateBlock *recording = device->recording_result;
        CHECK(!device_set_fvf(&device->iface,0) && !recording->has_vertex_format && !recording->vertex_declaration && device->vertex_declaration == old_decl);
        CHECK(!device_set_fvf(&device->iface,68) && recording->has_vertex_format && recording->vertex_declaration == views[3] && recording->fvf == 68 && device->vertex_declaration == old_decl);
        Interface *block = NULL;CHECK(!device_end_state_block(&device->iface,&block));
        CHECK(!device_set_fvf(&device->iface,4) && !state_block_apply_method(block) && device->vertex_declaration == views[3] && device->fvf == 68);
        CHECK(!state_block_capture_method(block) && !state_block_release(block) && device->refcount == 1);
        Interface *held = NULL;CHECK(!device_get_vertex_declaration(&device->iface,&held) && held == &views[3]->iface);
        device_clear_bindings(device);CHECK(!device->vertex_declaration && !device->fvf && views[3]->refcount == 1 && views[3]->binding_refs == 1 && !fvf_metadata(held,3));
        Interface *empty = (void*)1;CHECK(!device_get_vertex_declaration(&device->iface,&empty) && !empty);
        CHECK(!device_set_fvf(&device->iface,68) && device->vertex_declaration == views[3] && views[3]->refcount == 1 && !decl_release(held));
        CHECK(!device_set_vertex_declaration(&device->iface,&views[1]->iface) && device->fvf == 4);
        D9VertexDeclaration *normal = new_declaration(device);CHECK(!device_set_vertex_declaration(&device->iface,&normal->iface) && !device->fvf);
        CHECK(!decl_release(&normal->iface) && !device_set_fvf(&device->iface,68) && destroys == old_destroys + 1);
        if (order == 0) {
            device_clear_bindings(device);device_clear_fvf_cache(device);
            CHECK(!device->vertex_declarations && destroys == old_destroys + 7 && device->refcount == 1);
            CHECK(!device_release(&device->iface) && !live && !parent.refs);
        } else if (order == 1) {
            CHECK(!device_release(&device->iface) && !live && !parent.refs && destroys == old_destroys + 7);
        } else if (order == 2) {
            CHECK(!device_get_vertex_declaration(&device->iface,&held) && held == &views[3]->iface);
            device_clear_bindings(device);device_clear_fvf_cache(device);
            CHECK(device->vertex_declarations == views[3] && !views[3]->fvf_cache_ref && !views[3]->binding_refs && views[3]->refcount == 1 && destroys == old_destroys + 6);
            device_clear_fvf_cache(device);CHECK(!views[3]->binding_refs && destroys == old_destroys + 6);
            CHECK(!device_set_fvf(&device->iface,68) && device->vertex_declaration == views[3] && views[3]->fvf_cache_ref && views[3]->binding_refs == 2 && fvf_creates == old_creates + 6);
            CHECK(!decl_release(held) && !device_release(&device->iface) && !live && !parent.refs && destroys == old_destroys + 7);
        } else {
            CHECK(!device_get_vertex_declaration(&device->iface,&held) && device_release(&device->iface) == 1 && device->vertex_declaration == views[3] && views[3]->binding_refs == 2);
            CHECK(!decl_release(held) && !live && !parent.refs && destroys == old_destroys + 7);
        }
        profiles++;
    }
    printf("PASS actual D3D9 FVF declaration cache: %u checks, %u final-owner profiles, %u cached switches, %u public-zero revivals; six independent layouts, exact identity and device pins, zero no-op, recorded/captured FVF, reset-style retention, atomic failures and idempotent cache teardown\n",checks-old_checks,profiles,switches,revivals);
    return 0;
}
