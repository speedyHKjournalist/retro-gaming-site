static UINT pv8_checks,pv8_cases;
#define PV8_CHECK(v) do{if(!(v)){fprintf(stderr,"Process8 core line %u: %s\n",__LINE__,#v);return 1;}pv8_checks++;}while(0)
static uint32_t pv8_seed=0x391e0a17;
static uint32_t pv8_random(void){pv8_seed^=pv8_seed<<13;pv8_seed^=pv8_seed>>17;pv8_seed^=pv8_seed<<5;return pv8_seed;}
static int pv8_layout_cases(void)
{
    typedef struct{UINT before;D9WGProcessLayout layout;UINT after;}Guarded;
    for(UINT i=0;i<362144;i++) {
        UINT fvf=i<262144?(i&0xffffu)|((i>>16)*0x55550000u):pv8_random();
        Guarded old,now;memset(&old,0xa5,sizeof(old));now=old;
        BOOL a=oracle_decode_fvf_layout(fvf,&old.layout),b=d9wg_process_fvf8_subset(fvf,&now.layout);
        PV8_CHECK(a==b&&!memcmp(&old,&now,sizeof(old))&&now.before==0xa5a5a5a5u&&now.after==0xa5a5a5a5u);
        if(b)PV8_CHECK((now.layout.position_transformed==(!!((fvf&0xeu)==4)))&&now.layout.stride>0);
        pv8_cases++;
    }
    return 0;
}
typedef struct PV8Fixture{
    D9Device device;D9VertexBuffer source,destination;D9VertexDeclaration input,output;
    BYTE source_bytes[256],destination_bytes[256],expected[256];
    UINT src,dst,count,flags;BOOL alias;
}PV8Fixture;
static void pv8_init(PV8Fixture*f,UINT variant,BOOL alias,UINT src,UINT dst,UINT count,UINT flags,UINT transform)
{
    memset(f,0,sizeof(*f));f->alias=alias;f->src=src;f->dst=dst;f->count=count;f->flags=flags;
    D9Device*d=&f->device;d->handle=0x15379;d->viewport.X=3;d->viewport.Y=5;d->viewport.Width=32;d->viewport.Height=16;d->viewport.MinZ=.25f;d->viewport.MaxZ=.75f;
    for(UINT i=0;i<16;i++)d->streams[i].frequency=1;
    for(UINT i=0;i<266;i++)for(UINT j=0;j<16;j++)d->transforms[i][j]=(j%5)==0;
    d->transforms[D3DTS_WORLD][0]=transform%2?2:.5f;d->transforms[D3DTS_WORLD][12]=.25f;d->transforms[D3DTS_WORLD][13]=.125f;
    d->transforms[D3DTS_VIEW][12]=-.125f;d->transforms[D3DTS_VIEW][13]=.125f;
    d->transforms[D3DTS_PROJECTION][15]=transform/2==0?1:transform/2==1?2:0;
    f->source.iface.lpVtbl=f->destination.iface.lpVtbl=&g_vb_vtbl;
    f->source.device=f->destination.device=d;f->source.shadow=f->source_bytes;f->destination.shadow=f->destination_bytes;
    f->source.length=f->destination.length=256;f->source.handle=0x23;f->destination.handle=0x29;
    f->destination.fvf=variant==0||variant==2?0x4144u:0x144u;
    f->input.device=f->output.device=d;f->input.element_count=f->output.element_count=3;
    f->input.elements[0].Type=2;f->input.elements[1].Offset=12;f->input.elements[1].Type=4;f->input.elements[1].Usage=10;f->input.elements[2].Offset=16;f->input.elements[2].Type=1;f->input.elements[2].Usage=5;
    f->output.elements[0].Type=3;f->output.elements[0].Usage=9;f->output.elements[1].Offset=16;f->output.elements[1].Type=4;f->output.elements[1].Usage=10;f->output.elements[2].Offset=20;f->output.elements[2].Type=1;f->output.elements[2].Usage=5;
    d->vertex_declaration=&f->input;d->streams[0].buffer=alias?&f->destination:&f->source;d->streams[0].offset=4;d->streams[0].stride=24;
    memset(f->source_bytes,0xd5,sizeof(f->source_bytes));memset(f->destination_bytes,0xcc,sizeof(f->destination_bytes));
    BYTE*storage=alias?f->destination_bytes:f->source_bytes;
    const DWORD colors[]={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffffffu,0xffffff00u,0xff00ffffu};
    for(UINT i=0;i<6;i++) {float v[3]={((float)i-2)*.125f,(2-(float)i)*.125f,.25f+(i%3)*.125f},tex[2]={i*.125f,1-i*.125f};memcpy(storage+4+i*24,v,12);memcpy(storage+4+i*24+12,colors+i,4);memcpy(storage+4+i*24+16,tex,8);}
    memcpy(f->expected,f->destination_bytes,sizeof(f->expected));
    for(UINT i=0;i<count;i++) {UINT vertex=src+i;float x=((float)vertex-2)*.125f,y=(2-(float)vertex)*.125f,z=.25f+(vertex%3)*.125f,rhw=transform/2==1?.5f:1;float screen[4]={((x*(transform%2?2:.5f)+.125f)*rhw*.5f+.5f)*32+3,(.5f-(y+.25f)*rhw*.5f)*16+5,.25f+z*rhw*.5f,rhw};memcpy(f->expected+(dst+i)*28,screen,16);if(!(flags&1)){memcpy(f->expected+(dst+i)*28+16,storage+4+vertex*24+12,4);memcpy(f->expected+(dst+i)*28+20,storage+4+vertex*24+16,8);}}
    uploads=sequence_count=emits=invalidations=upload_fail_at=0;transport_ok=1;process_attempts=process_fail_at=process_allocations=process_frees=0;process_lighting_failure=0;
    active_api=8;
}
static HRESULT pv8_call(PV8Fixture*f,UINT variant)
{
    if(variant==0)return d9wg_d3d9_core_process_vertices8(&f->device,f->src,f->dst,f->count,&f->destination.iface,f->flags);
    IDirect3DVertexDeclaration9*out=variant==2?&f->output.iface:NULL;
    if(variant==3)return oracle_process_vertices(&f->device,f->src,f->dst,f->count,&f->destination.iface,NULL,f->flags);
    return device_process_vertices(&f->device,f->src,f->dst,f->count,&f->destination.iface,out,f->flags);
}
static int pv8_execution_cases(void)
{
    PV8Fixture f;D9WGClipStatus golden_status={0};BOOL golden_valid=FALSE;
    const DWORD flags[]={0,1,0x80000000u,0xffffffffu};
    for(UINT alias=0;alias<2;alias++)for(UINT src=1;src<=2;src++)for(UINT dst=1;dst<=2;dst++)for(UINT count=1;count<=3;count++)for(UINT flag=0;flag<4;flag++)for(UINT matrix=0;matrix<6;matrix++) {
        /* First run the frozen9 factory path, then the factored9 path with
         * FVF/explicit output declaration, andfinally the8 raw-bit14 path. */
        const UINT variants[]={3,1,2,0};
        for(UINT step=0;step<4;step++) {UINT variant=variants[step];pv8_init(&f,variant,alias,src,dst,count,flags[flag],matrix);
            PV8_CHECK(!pv8_call(&f,variant));PV8_CHECK(!memcmp(f.destination_bytes,f.expected,sizeof(f.expected)));
            PV8_CHECK(uploads==1&&process_upload_handle==0x29&&process_upload_offset==dst*28&&process_upload_size==count*28&&!process_upload_flags&&!memcmp(process_upload_bytes,f.expected+dst*28,count*28));
            PV8_CHECK(f.device.vertex_declaration==&f.input&&f.device.streams[0].buffer==(alias?&f.destination:&f.source)&&!f.device.vertex_shader&&process_live==0&&process_allocations==process_frees);
            if(step==0){golden_status=f.device.clip_status;golden_valid=f.device.clip_status_valid;}else PV8_CHECK(!memcmp(&golden_status,&f.device.clip_status,sizeof(golden_status))&&golden_valid==f.device.clip_status_valid);
            pv8_cases++;
        }
    }
    /* Ordinary9 still refuses the same raw0x4144 FVF when no declaration is
     * supplied; explicit9 declaration andnormalized9 FVF remainvalid. */
    for(UINT variant=1;variant<=3;variant+=2){pv8_init(&f,variant,0,1,1,2,0,0);f.destination.fvf=0x4144;BYTE old[256];memcpy(old,f.destination_bytes,256);PV8_CHECK(pv8_call(&f,variant)==D3DERR_INVALIDCALL&&!uploads&&!memcmp(old,f.destination_bytes,256)&&!process_live);pv8_cases++;}
    for(UINT failure=0;failure<16;failure++) {
        pv8_init(&f,0,0,1,1,2,0,0);BYTE old[256];memcpy(old,f.destination_bytes,256);D9Device other={0};int alien;
        IDirect3DDevice9*device=&f.device;IDirect3DVertexBuffer9*destination=&f.destination.iface;UINT count=f.count;
        switch(failure){case 0:device=NULL;break;case 1:active_api=9;break;case 2:destination=NULL;break;case 3:f.destination.iface.lpVtbl=&alien;break;case 4:f.destination.device=&other;break;case 5:f.destination.locked=1;break;case 6:count=0;break;case 7:f.device.vertex_shader=(void*)1;break;case 8:f.destination.fvf=0x4154;break;case 9:f.destination.fvf=0x4142;break;case 10:f.dst=UINT32_MAX;break;case 11:f.src=UINT32_MAX;break;case 12:f.source.locked=1;break;case 13:f.source.device=&other;break;case 14:f.device.streams[0].stride=23;break;default:process_lighting_failure=1;break;}
        PV8_CHECK(d9wg_d3d9_core_process_vertices8(device,f.src,f.dst,count,destination,f.flags)==D3DERR_INVALIDCALL);
        PV8_CHECK(!memcmp(old,f.destination_bytes,256)&&!uploads&&!process_live);pv8_cases++;
    }
    for(UINT allocation=1;allocation<=2;allocation++) {pv8_init(&f,0,1,1,2,3,0,0);BYTE old[256];memcpy(old,f.destination_bytes,256);process_fail_at=allocation;PV8_CHECK(pv8_call(&f,0)==E_OUTOFMEMORY&&!uploads&&!memcmp(old,f.destination_bytes,256)&&!process_live);pv8_cases++;}
    pv8_init(&f,0,0,1,2,3,0,0);transport_ok=0;PV8_CHECK(pv8_call(&f,0)==D3DERR_DRIVERINTERNALERROR&&uploads==1&&!memcmp(f.expected,f.destination_bytes,256)&&f.device.clip_status_valid&&!process_live);pv8_cases++;
    active_api=9;transport_ok=1;process_fail_at=0;
    return 0;
}
int main(void)
{
    CHECK(!retained_bound_draw_main());PV8_CHECK(!process_live);
    PV8_CHECK(!pv8_layout_cases());UINT layouts=pv8_cases;
    PV8_CHECK(!pv8_execution_cases());
    printf("PASS actual core8 ProcessVertices: %u checks / %u cases (%u frozen8 layouts/%u execution-control cases); independent projected coordinates/complete bytes,raw8 bit14 versusnormalized/explicit9/frozen9,alias snapshots/offsets/attributes/flags/zeroW,guarded failures/OOM/transport and exact scratch cleanup\n",pv8_checks,pv8_cases,layouts,pv8_cases-layouts);
    return 0;
}
