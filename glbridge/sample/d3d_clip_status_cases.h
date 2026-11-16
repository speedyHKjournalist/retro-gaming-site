/* Included by the SDK-specific D3D8/9 COM probe. */
#if D9WG_CLIP_API == 9
typedef D3DCLIPSTATUS9 Status;
#else
typedef D3DCLIPSTATUS8 Status;
#endif
static void status_reset(void)
{
    Status value={0,0xffffffffu};OK(CALL(SetClipStatus)(device,&value));
}
static void status_expect(DWORD clip_union,DWORD clip_intersection,const char *label)
{
    Status value={0xdeadbeefu,0xdeadbeefu};OK(CALL(GetClipStatus)(device,&value));
    if(value.ClipUnion!=clip_union || value.ClipIntersection!=clip_intersection){
        char text[180];wsprintfA(text,"FAIL API %u %s got=%08lx,%08lx expected=%08lx,%08lx",D9WG_CLIP_API,label,value.ClipUnion,value.ClipIntersection,clip_union,clip_intersection);report(text);ExitProcess(1);
    }
}
static Buffer *status_buffer(DWORD fvf,UINT bytes)
{
    Buffer *buffer;
#if D9WG_CLIP_API == 9
    OK(CALL(CreateVertexBuffer)(device,bytes,D3DUSAGE_SOFTWAREPROCESSING,fvf,D3DPOOL_SYSTEMMEM,&buffer,NULL));
#else
    OK(CALL(CreateVertexBuffer)(device,bytes,D3DUSAGE_SOFTWAREPROCESSING,fvf,D3DPOOL_SYSTEMMEM,&buffer));
#endif
    return buffer;
}
static void status_fill(Buffer *buffer,const void *data,UINT bytes)
{void *mapped;OK(BUF(Lock)(buffer,0,bytes,(void *)&mapped,0));CopyMemory(mapped,data,bytes);OK(BUF(Unlock)(buffer));}
static void status_process(Buffer *destination,UINT first)
{
#if D9WG_CLIP_API == 9
    OK(CALL(ProcessVertices)(device,first,2,3,destination,NULL,0));
#else
    OK(CALL(ProcessVertices)(device,first,2,3,destination,0));
#endif
}
static void status_draw_up(const void *data,UINT stride)
{OK(CALL(BeginScene)(device));OK(CALL(DrawPrimitiveUP)(device,D3DPT_TRIANGLELIST,1,data,stride));OK(CALL(EndScene)(device));}
static void clip_status_cases(void)
{
    status_expect(0,0xffffffffu,"default clip status");
    Status manual={0x800u,0x1001u};OK(CALL(SetClipStatus)(device,&manual));status_expect(0x800u,0x1001u,"exact Set/Get");
    check(FAILED(CALL(SetClipStatus)(device,NULL)) && FAILED(CALL(GetClipStatus)(device,NULL)),"NULL status refused");
    D3DMATRIX identity;ZeroMemory(&identity,sizeof(identity));identity._11=identity._22=identity._33=identity._44=1;
    OK(CALL(SetTransform)(device,D3DTS_WORLD,&identity));OK(CALL(SetTransform)(device,D3DTS_VIEW,&identity));OK(CALL(SetTransform)(device,D3DTS_PROJECTION,&identity));
    OK(CALL(SetRenderState)(device,D3DRS_LIGHTING,FALSE));OK(CALL(SetRenderState)(device,D3DRS_CLIPPING,TRUE));
    Viewport vp={7,11,100,80,.25f,.75f};OK(CALL(SetViewport)(device,&vp));
    const float positions[7][3]={{4,0,.5f},{3,0,.5f},{-2,-.5f,.5f},{-3,.5f,.5f},{-4,0,.5f},{0,3,.5f},{0,0,.5f}};
    Buffer *source=status_buffer(D3DFVF_XYZ,sizeof(positions)),*destination=status_buffer(D3DFVF_XYZRHW,128);
    status_fill(source,positions,sizeof(positions));set_fvf(D3DFVF_XYZ);stream(source,D9WG_CLIP_API==9?12:0,12);
    status_reset();OK(CALL(BeginScene)(device));OK(CALL(DrawPrimitive)(device,D3DPT_TRIANGLELIST,D9WG_CLIP_API==9?1:2,1));OK(CALL(EndScene)(device));status_expect(1,1,"nonindexed buffer and offset");
    for(unsigned wide=0;wide<2;++wide){
        Indices *buffer;WORD narrow[4]={0xffff,D9WG_CLIP_API==9?3:1,D9WG_CLIP_API==9?4:2,D9WG_CLIP_API==9?5:3};
        DWORD broad[4]={0xffffffff,D9WG_CLIP_API==9?3:1,D9WG_CLIP_API==9?4:2,D9WG_CLIP_API==9?5:3};
        UINT bytes=wide?sizeof(broad):sizeof(narrow);
#if D9WG_CLIP_API == 9
        OK(CALL(CreateIndexBuffer)(device,bytes,0,wide?D3DFMT_INDEX32:D3DFMT_INDEX16,D3DPOOL_SYSTEMMEM,&buffer,NULL));
#else
        OK(CALL(CreateIndexBuffer)(device,bytes,0,wide?D3DFMT_INDEX32:D3DFMT_INDEX16,D3DPOOL_SYSTEMMEM,&buffer));
#endif
        void *mapped;OK(IB(Lock)(buffer,0,0,(void *)&mapped,0));CopyMemory(mapped,wide?(void *)broad:(void *)narrow,bytes);OK(IB(Unlock)(buffer));indices(buffer);status_reset();
        OK(CALL(BeginScene)(device));
#if D9WG_CLIP_API == 9
        OK(CALL(DrawIndexedPrimitive)(device,D3DPT_TRIANGLELIST,-2,3,3,1,1));
#else
        OK(CALL(DrawIndexedPrimitive)(device,D3DPT_TRIANGLELIST,1,3,1,1));
#endif
        OK(CALL(EndScene)(device));status_expect(1,1,"indexed offset/base/min/start");IB(Release)(buffer);
    }
    status_reset();status_draw_up(positions[2],12);status_expect(1,1,"DrawPrimitiveUP");
    for(unsigned wide=0;wide<2;++wide){
        WORD narrow[3]={2,3,4};DWORD broad[3]={2,3,4};status_reset();
        OK(CALL(BeginScene)(device));OK(CALL(DrawIndexedPrimitiveUP)(device,D3DPT_TRIANGLELIST,2,3,1,wide?(void *)broad:(void *)narrow,wide?D3DFMT_INDEX32:D3DFMT_INDEX16,positions,12));OK(CALL(EndScene)(device));
        status_expect(1,1,"DrawIndexedPrimitiveUP");
    }
    {
        WORD selected[3]={2,3,4};status_reset();OK(CALL(BeginScene)(device));
        OK(CALL(DrawIndexedPrimitiveUP)(device,D3DPT_TRIANGLELIST,0,7,1,selected,D3DFMT_INDEX16,positions,12));
        OK(CALL(EndScene)(device));status_expect(7,0,"declared range includes unreferenced vertices");
    }
    stream(source,D9WG_CLIP_API==9?12:0,12);status_reset();status_process(destination,D9WG_CLIP_API==9?1:2);status_expect(1,1,"processed clip codes");
    status_reset();set_fvf(D3DFVF_XYZRHW);stream(destination,0,16);
    OK(CALL(BeginScene)(device));OK(CALL(DrawPrimitive)(device,D3DPT_TRIANGLELIST,2,1));OK(CALL(EndScene)(device));status_expect(0,0xffffffffu,"processed VB draw does not repeat processing");
#if D9WG_CLIP_API == 9
    {
        const DWORD code[]={0xfffe0101u,1u,0xc00f0000u,0x90e40000u,0x0000ffffu};
        IDirect3DVertexShader9 *shader;OK(CALL(CreateVertexShader)(device,code,&shader));OK(CALL(SetVertexShader)(device,shader));
        status_reset();OK(CALL(BeginScene)(device));OK(CALL(DrawPrimitive)(device,D3DPT_TRIANGLELIST,2,1));OK(CALL(EndScene)(device));
        status_expect(0,0xffffffffu,"POSITIONT bypasses bound vertex program");
        OK(CALL(SetVertexShader)(device,NULL));IDirect3DVertexShader9_Release(shader);
    }
#endif
    set_fvf(D3DFVF_XYZ);stream(source,D9WG_CLIP_API==9?12:0,12);OK(CALL(SetRenderState)(device,D3DRS_CLIPPING,FALSE));status_reset();status_process(destination,D9WG_CLIP_API==9?1:2);status_expect(0,0,"disabled processing");
    OK(CALL(SetClipStatus)(device,&manual));status_draw_up(positions[2],12);status_expect(0,0,"disabled drawing");
    OK(CALL(SetRenderState)(device,D3DRS_CLIPPING,TRUE));
    const float inside[3][3]={{-.5f,-.5f,.5f},{-.25f,.5f,.5f},{-.75f,0,.5f}},plane[4]={1,0,0,0};
    D3DMATRIX world=identity,view=identity;world._41=.5f;view._41=-.5f;
    OK(CALL(SetTransform)(device,D3DTS_WORLD,&world));OK(CALL(SetTransform)(device,D3DTS_VIEW,&view));OK(CALL(SetClipPlane)(device,0,plane));OK(CALL(SetRenderState)(device,D3DRS_CLIPPLANEENABLE,1));
    status_reset();status_draw_up(inside,12);status_expect(64,0,"user planes use world coordinates");
    struct BlendInput{float position[3],weight;BYTE indices[4];} blend[3]={{{0,0,.5f},.25f,{0,1,0,0}},{{0,0,.5f},.25f,{0,1,0,0}},{{0,0,.5f},.25f,{0,1,0,0}}};
    world=identity;world._41=-1;D3DMATRIX world1=identity;world1._41=1;view=identity;view._41=-.25f;D3DMATRIX projection=identity;projection._33=0;const float reverse_plane[4]={-1,0,0,0};
    OK(CALL(SetTransform)(device,D3DTS_WORLD,&world));OK(CALL(SetTransform)(device,D3DTS_WORLDMATRIX(1),&world1));OK(CALL(SetTransform)(device,D3DTS_VIEW,&view));OK(CALL(SetTransform)(device,D3DTS_PROJECTION,&projection));
    OK(CALL(SetClipPlane)(device,0,reverse_plane));OK(CALL(SetRenderState)(device,D3DRS_VERTEXBLEND,1));OK(CALL(SetRenderState)(device,D3DRS_INDEXEDVERTEXBLENDENABLE,TRUE));
    set_fvf(0x1008u);status_reset();status_draw_up(blend,sizeof(blend[0]));status_expect(64,64,"indexed world blend and singular projection");
    OK(CALL(SetRenderState)(device,D3DRS_VERTEXBLEND,0));OK(CALL(SetRenderState)(device,D3DRS_INDEXEDVERTEXBLENDENABLE,FALSE));OK(CALL(SetRenderState)(device,D3DRS_CLIPPLANEENABLE,0));
    OK(CALL(SetTransform)(device,D3DTS_WORLD,&identity));OK(CALL(SetTransform)(device,D3DTS_VIEW,&identity));OK(CALL(SetTransform)(device,D3DTS_PROJECTION,&identity));
#if D9WG_CLIP_API == 9
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{1,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9 *decl;OK(CALL(CreateVertexDeclaration)(device,elements,&decl));OK(CALL(SetVertexDeclaration)(device,decl));stream(source,12,12);OK(CALL(SetStreamSource)(device,1,source,0,12));
    status_reset();OK(CALL(BeginScene)(device));OK(CALL(DrawPrimitive)(device,D3DPT_TRIANGLELIST,1,1));OK(CALL(EndScene)(device));
    /* The shared stream pipeline now supports this declaration. The stream0
     * byte offset plus first vertex selects positions[2..4], all left of x=-1;
     * the separate normal stream cannot change those identity-space positions. */
    status_expect(1,1,"multi-stream position offset and normal stream");
    {
        const DWORD code[]={0xfffe0101u,1u,0xc00f0000u,0x90e40000u,0x0000ffffu};
        IDirect3DVertexShader9 *shader;OK(CALL(CreateVertexShader)(device,code,&shader));OK(CALL(SetVertexShader)(device,shader));
        status_reset();OK(CALL(BeginScene)(device));OK(CALL(DrawPrimitive)(device,D3DPT_TRIANGLELIST,1,1));OK(CALL(EndScene)(device));
        Status unavailable;check(FAILED(CALL(GetClipStatus)(device,&unavailable)),"unsupported CPU vertex program cannot return stale status");
        OK(CALL(SetVertexShader)(device,NULL));IDirect3DVertexShader9_Release(shader);
    }
    IDirect3DVertexDeclaration9_Release(decl);
#endif
    set_fvf(D3DFVF_XYZ);status_reset();status_draw_up(positions[2],12);status_expect(1,1,"status recovers after reset");
    set_fvf(D3DFVF_XYZRHW);float screen[3][4]={{-5,40,.5f,1},{-10,60,.5f,1},{-15,50,.5f,1}};
    status_reset();status_draw_up(screen,16);status_expect(1,1,"immediate transformed clipping");
    OK(CALL(SetRenderState)(device,D3DRS_CLIPPING,FALSE));status_reset();status_draw_up(screen,16);status_expect(0,0xffffffffu,"disabled immediate transformed path bypasses processing");
    OK(CALL(SetRenderState)(device,D3DRS_CLIPPING,TRUE));screen[0][0]=32;screen[1][0]=55;screen[2][0]=42;
    OK(CALL(SetClipPlane)(device,0,reverse_plane));OK(CALL(SetRenderState)(device,D3DRS_CLIPPLANEENABLE,1));
    status_reset();status_draw_up(screen,16);status_expect(0,0,"transformed clip codes ignore world planes");
    OK(CALL(SetRenderState)(device,D3DRS_CLIPPLANEENABLE,0));const float screen_depths[5]={.1f,.9f,-.1f,1.1f,.5f};
    for(unsigned test=0;test<5;++test){for(unsigned i=0;i<3;++i)screen[i][2]=screen_depths[test];if(test==4){vp.MinZ=vp.MaxZ=0;OK(CALL(SetViewport)(device,&vp));}
        status_reset();status_draw_up(screen,16);DWORD expected=test==2?16:test==3?32:0;status_expect(expected,expected,"screen depth clip codes bypass mapped range");}

    BUF(Release)(source);BUF(Release)(destination);report("PASS real XP D3D8/9 clip status cases");
}
