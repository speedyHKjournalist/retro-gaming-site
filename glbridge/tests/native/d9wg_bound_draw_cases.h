int main(void)
{
    static D9Device device, other;
    for (unsigned i = 0; i < 16; ++i) device.streams[i].frequency = 1;
    for (unsigned i = 0; i < 266; ++i)
        for (unsigned j = 0; j < 16; ++j) device.transforms[i][j] = j % 5 == 0;
    device.fvf = 2;device.creation.BehaviorFlags = 0x20;device.render_states[136] = 1;
    device.viewport.Width = device.viewport.Height = 64;device.viewport.MaxZ = 1;
    float data[12] = {0, 0, .5f, -2, 0, .5f, 2, 0, .5f, 0, 0, .5f};
    D9VertexBuffer vb = {.device=&device, .shadow=(void*)data, .length=sizeof(data)};
    uint16_t indices[3] = {1, 1, 1};
    D9IndexBuffer ib = {.device=&device, .shadow=(void*)indices, .length=6, .format=101};
    device.streams[0].buffer = &vb;device.streams[0].stride = 12;device.index_buffer = &ib;
    const uint32_t hints[][2] = {{0,0}, {0xffffffffu,0}, {0xffffffffu,2}, {17,1}, {0,4}};
    for (unsigned i=0;i<5;++i) {
        device.clip_status=(D9WGClipStatus){0,UINT32_MAX};device.clip_status_valid=TRUE;
        CHECK(device_draw_indexed_primitive(&device,4,0,hints[i][0],hints[i][1],0,1)==0);
        if(i==4)CHECK(device.clip_status_valid&&device.clip_status.clip_union==3&&device.clip_status.clip_intersection==0);else CHECK(!device.clip_status_valid);
        CHECK(indexed.min_vertex_index==hints[i][0]&&indexed.vertex_count==hints[i][1]&&indexed.base_vertex_index==0);
    }
    device.clip_status_valid=FALSE;
    CHECK(device_draw_indexed_primitive(&device,4,0,0,4,0,1)==0&&device.clip_status_valid);
    indices[0]=indices[1]=indices[2]=2;
    device.clip_status=(D9WGClipStatus){0,UINT32_MAX};
    CHECK(device_draw_indexed_primitive(&device,4,-1,2,1,0,1)==0&&device.clip_status.clip_union==1&&indexed.base_vertex_index==-1);
    device.streams[0].offset=12;device.clip_status=(D9WGClipStatus){0,UINT32_MAX};
    CHECK(device_draw_indexed_primitive(&device,4,-1,2,1,0,1)==0&&device.clip_status.clip_union==2);
    unsigned before=emits, mirrors=invalidations;D9WGClipStatus status=device.clip_status;
    indices[0]=0;
    CHECK(device_draw_indexed_primitive(&device,4,-1,0,0,0,1)==0&&!device.clip_status_valid);before=emits;mirrors=invalidations;status=device.clip_status;
    indices[0]=indices[1]=indices[2]=4;
    CHECK(device_draw_indexed_primitive(&device,4,0,0,0,0,1)==0&&!device.clip_status_valid);before=emits;mirrors=invalidations;status=device.clip_status;
    for(unsigned type=1;type<=6;++type){
        CHECK(device_draw_indexed_primitive(&device,type,INT32_MIN,UINT32_MAX,0,UINT32_MAX,0)==0);
        CHECK(device_draw_primitive(&device,type,UINT32_MAX,0)==0);
    }
    CHECK(emits==before&&invalidations==mirrors&&!memcmp(&status,&device.clip_status,sizeof(status)));
    CHECK(device_draw_indexed_primitive(&device,0,0,0,0,0,0)==-1);
    CHECK(device_draw_primitive(&device,7,0,0)==-1);
    device.fvf=0;CHECK(device_draw_indexed_primitive(&device,4,0,0,0,0,0)==-1);device.fvf=2;
    device.index_buffer=NULL;CHECK(device_draw_indexed_primitive(&device,4,0,0,0,0,0)==-1);device.index_buffer=&ib;
    CHECK(device_draw_indexed_primitive(&device,4,0,0,0,1,1)==-1&&emits==before);
    indices[0]=indices[1]=indices[2]=1;device.streams[0].offset=0;
    transport_ok=0;CHECK(device_draw_indexed_primitive(&device,4,-1,0,0,0,1)==-3&&emits==before+1&&invalidations==mirrors&&!memcmp(&status,&device.clip_status,sizeof(status)));transport_ok=1;
    vb.device=&other;before=emits;CHECK(device_draw_indexed_primitive(&device,4,0,0,0,0,1)==-1&&emits==before);vb.device=&device;
    /* Programmable declarations need byte extents, without a POSITION0. */
    device.creation.BehaviorFlags=0;D9VertexDeclaration declaration={.element_count=2,.elements={{15,4,0,0,5,0},{7,0,4,0,10,0}}};device.vertex_declaration=&declaration;
    device.streams[15].buffer=&vb;device.streams[15].stride=12;
    D9VertexBuffer instance={.device=&device,.shadow=(void*)data,.length=8};device.streams[7].buffer=&instance;device.streams[7].stride=4;
    device.streams[0].frequency=0x40000004u;device.streams[7].frequency=0x80000002u;
    CHECK(device_draw_indexed_primitive(&device,4,0,UINT32_MAX,0,0,1)==0);
    instance.length=7;before=emits;CHECK(device_draw_indexed_primitive(&device,4,0,0,0,0,1)==-1&&emits==before);
    instance.length=4;device.streams[7].stride=0;CHECK(device_draw_indexed_primitive(&device,4,0,0,0,0,1)==0);
    CHECK(last_opcode==D9WG_OP_DRAW_INDEXED_PRIMITIVE);
    declaration.elements[0].Offset=0;declaration.elements[0].Type=2;declaration.elements[0].Usage=0;
    declaration.elements[1].Usage=10;device.streams[7].frequency=device.streams[0].frequency=1;
    device.creation.BehaviorFlags=0x20;device.clip_status=(D9WGClipStatus){0,UINT32_MAX};device.clip_status_valid=FALSE;
    CHECK(device_draw_indexed_primitive(&device,4,0,0,4,0,1)==0&&device.clip_status_valid&&device.clip_status.clip_union==3);
    CHECK(device_draw_indexed_primitive(&device,4,0,0,0,0,1)==0&&!device.clip_status_valid);
    CHECK(device_draw_indexed_primitive(&device,4,0,1,1,0,1)==0&&device.clip_status_valid);

    vb.handle=41;vb.lock_size=vb.length;instance.handle=42;instance.lock_size=instance.length;ib.handle=43;ib.lock_size=ib.length;
    vb.locked=instance.locked=ib.locked=TRUE;vb.lock_flags=instance.lock_flags=ib.lock_flags=8192;
    sequence_count=0;unsigned uploads_before=uploads;before=emits;mirrors=invalidations;
    CHECK(device_draw_indexed_primitive(&device,4,INT32_MIN,UINT32_MAX,0,UINT32_MAX,0)==0&&uploads==uploads_before&&!sequence_count&&emits==before&&invalidations==mirrors&&ib.lock_flags==8192);
    sequence_count=0;
    CHECK(device_draw_primitive(&device,1,1,1)==0&&sequence_count==3&&!memcmp(sequence,"UUD",3)&&device.clip_status_valid&&!vb.lock_flags&&!instance.lock_flags&&ib.lock_flags==8192&&vb.locked&&instance.locked&&ib.locked);
    vb.lock_flags=instance.lock_flags=8192;sequence_count=0;before=emits;mirrors=invalidations;
    device.clip_status=(D9WGClipStatus){0x80000000u,0x80000000u};status=device.clip_status;upload_fail_at=uploads+2;
    CHECK(device_draw_indexed_primitive(&device,4,0,1,1,0,1)==-3&&emits==before&&invalidations==mirrors&&!memcmp(&status,&device.clip_status,sizeof(status))&&sequence_count==2&&!memcmp(sequence,"UU",2)&&!vb.lock_flags&&instance.lock_flags==8192&&ib.lock_flags==8192);
    upload_fail_at=0;sequence_count=0;
    CHECK(device_draw_indexed_primitive(&device,4,0,1,1,0,1)==0&&sequence_count==4&&!memcmp(sequence,"UUUD",4)&&device.clip_status_valid&&vb.locked&&instance.locked&&ib.locked&&!vb.lock_flags&&!instance.lock_flags&&!ib.lock_flags);
    vb.locked=instance.locked=ib.locked=FALSE;
    declaration.elements[0].Usage=9;declaration.elements[0].Type=3;device.streams[15].stride=16;
    device.clip_status=(D9WGClipStatus){11,17};device.clip_status_valid=TRUE;
    CHECK(device_draw_indexed_primitive(&device,4,0,0,0,0,1)==0&&device.clip_status_valid&&device.clip_status.clip_union==11&&device.clip_status.clip_intersection==17);
    void *guard=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(guard!=MAP_FAILED);
    BYTE *saved_indices=ib.shadow;ib.shadow=guard;
    CHECK(device_draw_indexed_primitive(&device,4,0,0,3,0,1)==0);ib.shadow=saved_indices;CHECK(munmap(guard,4096)==0);
    declaration.element_count=0;device.vertex_shader=(void*)1;device.creation.BehaviorFlags=0;
    for(unsigned i=0;i<16;++i)device.streams[i].buffer=NULL;
    CHECK(device_draw_indexed_primitive(&device,4,0,0,0,0,1)==0&&last_opcode==D9WG_OP_DRAW_INDEXED_PRIMITIVE);
    /* NULL streams provide constants even when stale stride/offset remain.
     * All paths still submit real draw commands and retain non-NULL bounds. */
    device.vertex_shader=NULL;device.vertex_declaration=NULL;device.fvf=2;
    device.creation.BehaviorFlags=0x20;device.streams[0].stride=12;device.streams[0].offset=48;
    CHECK(device_draw_primitive(&device,4,0,2)==0&&last_opcode==D9WG_OP_DRAW_PRIMITIVE);
    CHECK(device_draw_indexed_primitive(&device,4,0,0,4,0,1)==0&&last_opcode==D9WG_OP_DRAW_INDEXED_PRIMITIVE);
    device.index_buffer=NULL;
    CHECK(device_draw_indexed_primitive(&device,4,0,0,4,0,1)==0&&last_opcode==D9WG_OP_DRAW_PRIMITIVE);
    device.fvf=0;CHECK(device_draw_indexed_primitive(&device,4,0,0,4,0,1)==-1);device.fvf=2;
    device.streams[0].buffer=&vb;device.streams[0].offset=0;
    CHECK(device_draw_indexed_primitive(&device,4,0,0,4,0,1)==-1);
    device.vertex_declaration=&declaration;declaration.element_count=2;
    declaration=(D9VertexDeclaration){.element_count=2,.elements={{0,0,2,0,0,0},{1,0,4,0,10,0}}};
    device.vertex_shader=(void*)1;
    CHECK(device_draw_primitive(&device,1,0,1)==0&&last_opcode==D9WG_OP_DRAW_PRIMITIVE);
    CHECK(device_draw_primitive(&device,1,4,1)==-1);
    puts("PASS actual D3D9 bound draw C: ignored/overflowing hints, NumVertices=0, signed base/offset and multi-stream CPU ClipStatus/recovery, six zero-primitive no-ops, exact IB/stream bounds, programmable/constant/instance declarations and atomic transport failure");return 0;
}
