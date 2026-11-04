static uint32_t indexed8_seed=0x31415926u;
static uint32_t indexed8_random(void){indexed8_seed^=indexed8_seed<<13;indexed8_seed^=indexed8_seed>>17;indexed8_seed^=indexed8_seed<<5;return indexed8_seed;}
#define INDEX8_CHECK(v) do{if(!(v)){fprintf(stderr,"Indexed8 core line %u: %s\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void)
{
    CHECK(!retained_bound_draw_main());
    UINT checks=0,cases=0,accepted=0,refused=0,zero=0,failures=0;
    static BYTE storage[1024];
    const UINT bases[]={0,1,0xffffffffu,2,0x7fffffffu,0x80000000u,0x80000001u,0xfffffff9u,0x7ffffffeu};
    for(UINT round=0;round<50000;round++) {
        D9Device d={0};for(UINT i=0;i<16;i++)d.streams[i].frequency=1;
        D9VertexDeclaration declaration={0};declaration.element_count=2;
        declaration.elements[0].Stream=7;declaration.elements[0].Offset=4;declaration.elements[0].Type=2;
        declaration.elements[1].Stream=15;declaration.elements[1].Offset=8;declaration.elements[1].Type=4;declaration.elements[1].Usage=10;
        d.vertex_declaration=&declaration;d.vertex_shader=(void*)1;d.handle=0x13579;
        D9VertexBuffer vb[2]={{0}};UINT stream_ids[]={7,15},extents[]={16,12};
        for(UINT i=0;i<2;i++){vb[i].device=&d;vb[i].shadow=storage;vb[i].length=1+indexed8_random()%512;d.streams[stream_ids[i]].buffer=&vb[i];d.streams[stream_ids[i]].offset=indexed8_random()%17;d.streams[stream_ids[i]].stride=indexed8_random()%33;}
        D9IndexBuffer ib={0};ib.device=&d;ib.shadow=storage;ib.format=round%2?101:102;ib.length=indexed8_random()%33;d.index_buffer=&ib;
        UINT raw=round%3?bases[round%9]:indexed8_random(),first=indexed8_random()%8,minimum=indexed8_random(),hint=indexed8_random();if(round%7==0)hint=0;
        UINT type=round%29==0?0:round%31==0?7:1,count=round%19==0?0:1;
        int64_t signed_value=raw<0x80000000u?(int64_t)raw:(int64_t)raw-0x100000000ll;
        BOOL geometry=FALSE,constant=TRUE,valid_streams=TRUE;
        for(UINT i=0;i<2;i++){UINT id=stream_ids[i];if((uint64_t)d.streams[id].offset+extents[i]>vb[i].length)valid_streams=FALSE;if(d.streams[id].stride){constant=FALSE;if(d.streams[id].stride<extents[i])valid_streams=FALSE;}}
        /* Enumerate physical vertices, independently of the production's
         * closed-form lower/upper bounds and signed conversion. */
        if(valid_streams){if(constant)geometry=TRUE;else for(UINT physical=0;physical<512;physical++){
            int64_t index=(int64_t)physical-signed_value;if(index<0||index>UINT32_MAX)continue;
            BOOL fits=TRUE;for(UINT i=0;i<2;i++){UINT id=stream_ids[i];uint64_t end=(uint64_t)d.streams[id].offset+extents[i]+(uint64_t)physical*d.streams[id].stride;if(end>vb[i].length)fits=FALSE;}if(fits){geometry=TRUE;break;}
        }}
        UINT size=ib.format==101?2:4;BOOL index_range=first<=ib.length/size&&count<=ib.length/size-first;
        BOOL accepted_call=type==1&&(!count||(geometry&&index_range));
        BOOL failed_transport=round%23==0&&accepted_call&&count;
        emits=invalidations=sequence_count=uploads=upload_fail_at=0;transport_ok=!failed_transport;
        D9WGClipStatus prior={0x13,0x17};d.clip_status=prior;d.clip_status_valid=TRUE;
        HRESULT hr=d9wg_d3d9_core_draw_indexed8(&d,type,raw,minimum,hint,first,count);

        if(!accepted_call){INDEX8_CHECK(hr==D3DERR_INVALIDCALL&&!emits&&!invalidations);refused++;}
        else if(!count){INDEX8_CHECK(!hr&&!emits&&!invalidations);zero++;}
        else {INDEX8_CHECK(hr==(failed_transport?D3DERR_DRIVERINTERNALERROR:D3D_OK));INDEX8_CHECK(emits==1&&last_opcode==D9WG_OP_DRAW_INDEXED_PRIMITIVE&&sequence_count==1&&sequence[0]=='D');INDEX8_CHECK(indexed.device_handle==d.handle&&indexed.primitive_type==type&&indexed.base_vertex_index==signed_value&&indexed.min_vertex_index==minimum&&indexed.vertex_count==hint&&indexed.start_index==first&&indexed.primitive_count==count);INDEX8_CHECK(invalidations==(failed_transport?0u:1u));if(failed_transport){INDEX8_CHECK(d.clip_status_valid&&memcmp(&d.clip_status,&prior,sizeof(prior))==0);failures++;}else accepted++;}
        INDEX8_CHECK(d.index_buffer==&ib&&d.vertex_declaration==&declaration&&d.vertex_shader==(void*)1&&!uploads);
        cases++;
    }
    D9Device d={0};UINT sent=emits,mirrors=invalidations;
    INDEX8_CHECK(d9wg_d3d9_core_draw_indexed8(NULL,1,0,0,0,0,1)==D3DERR_INVALIDCALL);
    D9VertexDeclaration empty={0};D9IndexBuffer valid_ib={0};valid_ib.device=&d;valid_ib.length=12;valid_ib.format=101;valid_ib.shadow=storage;d.index_buffer=&valid_ib;d.vertex_declaration=&empty;d.vertex_shader=(void*)1;transport_ok=1;sequence_count=0;
    INDEX8_CHECK(!device_draw_indexed_primitive(&d,1,0,0,0,0,1));sent=emits;mirrors=invalidations;
    active_api=9;INDEX8_CHECK(d9wg_d3d9_core_draw_indexed8(&d,1,0,0,0,0,1)==D3DERR_INVALIDCALL&&emits==sent&&invalidations==mirrors);
    active_api=8;d.index_buffer=NULL;INDEX8_CHECK(d9wg_d3d9_core_draw_indexed8(&d,1,0,0,0,0,0)==D3DERR_INVALIDCALL&&emits==sent);cases+=4;
    transport_ok=1;active_api=9;
    printf("PASS actual core8 indexed draw: %u checks / %u cases (%u emitted successes/%u refused/%u zero/%u transport failures); raw32 signed extrema,independent physical-vertex enumeration,multi-stream offsets/constant strides,16/32bit IB bounds/opaque hints,actual9 emitter/state and unchanged bindings\n",checks,cases,accepted,refused,zero,failures);
    return 0;
}
