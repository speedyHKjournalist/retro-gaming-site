/* Public COM scope checks. Expected classes come from the Wine Windows tests
 * and Microsoft ALL/vertex/pixel contracts, not the production scope header. */
#if D9WG_TNL_API == 9
#define SCOPE_ORDER D3DRS_POSITIONDEGREE
static void scope_bind(Buffer *buffer) {check(SUCCEEDED(CALL(SetStreamSource)(device,0,buffer,0,12)),"scope stream");}
static void scope_indices(IDirect3DIndexBuffer9 *buffer) {check(SUCCEEDED(CALL(SetIndices)(device,buffer)),"scope indices");}
typedef IDirect3DIndexBuffer9 ScopeIndex;
typedef IDirect3DTexture9 ScopeTexture;
#else
#define SCOPE_ORDER D3DRS_POSITIONORDER
static void scope_bind(Buffer *buffer) {check(SUCCEEDED(CALL(SetStreamSource)(device,0,buffer,12)),"scope stream");}
static void scope_indices(IDirect3DIndexBuffer8 *buffer) {check(SUCCEEDED(CALL(SetIndices)(device,buffer,7)),"scope indices");}
typedef IDirect3DIndexBuffer8 ScopeIndex;
typedef IDirect3DTexture8 ScopeTexture;
#endif
static void scope_fvf(DWORD fvf)
{
#if D9WG_TNL_API == 9
    check(SUCCEEDED(CALL(SetFVF)(device,fvf)),"scope FVF");
#else
    check(SUCCEEDED(CALL(SetVertexShader)(device,fvf)),"scope FVF");
#endif
}
static void scope_rs(D3DRENDERSTATETYPE name,DWORD expected)
{ DWORD got;check(SUCCEEDED(CALL(GetRenderState)(device,name,&got))&&got==expected,"predefined RS scope"); }
static void scope_tss(D3DTEXTURESTAGESTATETYPE name,DWORD expected)
{ DWORD got;check(SUCCEEDED(CALL(GetTextureStageState)(device,0,name,&got))&&got==expected,"predefined TSS scope"); }
static D3DTRANSFORMSTATETYPE scope_matrix_state(unsigned slot)
{ return slot==0?D3DTS_VIEW:slot==1?D3DTS_PROJECTION:slot<10?(D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0+slot-2):D3DTS_WORLDMATRIX(slot-10); }
static void scope_matrix_set(BOOL second)
{
    for(unsigned slot=0;slot<266;++slot){D3DMATRIX matrix;identity(&matrix);matrix._11=second?2:1;matrix._41=second?-(float)slot-.75f:(float)slot+.25f;check(SUCCEEDED(CALL(SetTransform)(device,scope_matrix_state(slot),&matrix)),"scope matrix set");}
}
static void scope_matrix_expect(BOOL second)
{
    for(unsigned slot=0;slot<266;++slot){D3DMATRIX got;check(SUCCEEDED(CALL(GetTransform)(device,scope_matrix_state(slot),&got)),"scope matrix get");check(got._11==(second?2:1)&&got._41==(second?-(float)slot-.75f:(float)slot+.25f),"all 266 matrix scope");}
}
static void scope_binding_expect(Buffer *expected,ScopeIndex *expected_indices,ScopeTexture *expected_texture)
{
    Buffer *got;UINT stride;ScopeIndex *indices;
#if D9WG_TNL_API == 9
    UINT offset;IDirect3DBaseTexture9 *texture;
    check(SUCCEEDED(CALL(GetStreamSource)(device,0,&got,&offset,&stride))&&offset==0,"scope stream query");
    check(SUCCEEDED(CALL(GetIndices)(device,&indices)),"scope indices query");
    check(SUCCEEDED(CALL(GetTexture)(device,0,&texture))&&texture==(IDirect3DBaseTexture9*)expected_texture,"scope texture query");IDirect3DBaseTexture9_Release(texture);
#else
    UINT base;IDirect3DBaseTexture8 *texture;
    check(SUCCEEDED(CALL(GetStreamSource)(device,0,&got,&stride)),"scope stream query");
    check(SUCCEEDED(CALL(GetIndices)(device,&indices,&base))&&base==7,"scope indices query");
    check(SUCCEEDED(CALL(GetTexture)(device,0,&texture))&&texture==(IDirect3DBaseTexture8*)expected_texture,"scope texture query");IDirect3DBaseTexture8_Release(texture);
#endif
    check(got==expected&&stride==12&&indices==expected_indices,"predefined binding isolation");BUF(Release)(got);
#if D9WG_TNL_API == 9
    IDirect3DIndexBuffer9_Release(indices);
#else
    IDirect3DIndexBuffer8_Release(indices);
#endif
}
static void shared_state_scope_checks(void)
{
    LightBlock baseline=light_block_create(D3DSBT_ALL);Buffer *buffers[2];ScopeIndex *indices[2];ScopeTexture *textures[2];
    for(unsigned i=0;i<2;++i){
        create_buffer(D3DFVF_XYZ,64,buffers+i);
#if D9WG_TNL_API == 9
        check(SUCCEEDED(CALL(CreateIndexBuffer)(device,32,0,D3DFMT_INDEX16,D3DPOOL_SYSTEMMEM,indices+i,NULL)),"scope IB create");
        check(SUCCEEDED(CALL(CreateTexture)(device,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,textures+i,NULL)),"scope texture create");
#else
        check(SUCCEEDED(CALL(CreateIndexBuffer)(device,32,0,D3DFMT_INDEX16,D3DPOOL_SYSTEMMEM,indices+i)),"scope IB create");
        check(SUCCEEDED(CALL(CreateTexture)(device,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,textures+i)),"scope texture create");
#endif
    }
    const D3DSTATEBLOCKTYPE kinds[]={D3DSBT_VERTEXSTATE,D3DSBT_PIXELSTATE,D3DSBT_ALL};
    Light a,b;ZeroMemory(&a,sizeof(a));a.Type=D3DLIGHT_DIRECTIONAL;a.Direction.z=1;a.Diffuse.r=.25f;b=a;b.Diffuse.g=.5f;
    Material material_a,material_b;ZeroMemory(&material_a,sizeof(material_a));material_a.Diffuse.r=.125f;material_b=material_a;material_b.Diffuse.r=.75f;
    const float plane_a[4]={-1,0,.5f,2},plane_b[4]={1,-2,.25f,3};
    for(unsigned row=0;row<3;++row){
        BOOL all=row==2,vertex=row!=1,pixel=row!=0;
#if D9WG_TNL_API == 9
        D3DVIEWPORT9 vp={0,0,48,48,.25f,.75f},got_vp;UINT frequency;
        check(SUCCEEDED(CALL(SetStreamSourceFreq)(device,0,1)),"scope initial frequency");check(SUCCEEDED(CALL(SetNPatchMode)(device,0)),"scope initial NPatch");
        check(SUCCEEDED(CALL(SetSamplerState)(device,0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP)),"scope initial filter");check(SUCCEEDED(CALL(SetSamplerState)(device,0,D3DSAMP_DMAPOFFSET,256)),"scope initial DMAP");
        state(D3DRS_CCW_STENCILFUNC,D3DCMP_ALWAYS);check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_CONSTANT,0xff010203u)),"scope initial stage constant");
#else
        D3DVIEWPORT8 vp={0,0,48,48,.25f,.75f},got_vp;
#endif
        state(D3DRS_CULLMODE,D3DCULL_CCW);state(D3DRS_TEXTUREFACTOR,0xff112233u);state(D3DRS_SHADEMODE,D3DSHADE_GOURAUD);state(D3DRS_SPECULARENABLE,FALSE);state(SCOPE_ORDER,1);
        check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_TEXCOORDINDEX,0)),"scope initial coordinates");check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_TEXTURETRANSFORMFLAGS,0)),"scope initial texture transform");check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_COLOROP,D3DTOP_MODULATE)),"scope initial colour op");
        check(SUCCEEDED(CALL(SetViewport)(device,&vp)),"scope initial viewport");check(SUCCEEDED(CALL(SetMaterial)(device,&material_a)),"scope initial material");check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_a)),"scope initial plane");
        check(SUCCEEDED(CALL(SetLight)(device,37,&a))&&SUCCEEDED(CALL(LightEnable)(device,37,FALSE)),"scope initial light");
        scope_bind(buffers[0]);scope_indices(indices[0]);
#if D9WG_TNL_API == 9
        check(SUCCEEDED(CALL(SetTexture)(device,0,(IDirect3DBaseTexture9*)textures[0])),"scope initial texture");
#else
        check(SUCCEEDED(CALL(SetTexture)(device,0,(IDirect3DBaseTexture8*)textures[0])),"scope initial texture");
#endif
        scope_fvf(D3DFVF_XYZ);scope_matrix_set(FALSE);LightBlock block=light_block_create(kinds[row]);
        const DWORD late_low=1+row,late_high=2000+row;
        check(SUCCEEDED(CALL(SetLight)(device,late_low,&a))&&SUCCEEDED(CALL(SetLight)(device,late_high,&a)),"lights created after snapshot");light_block_capture(block);
        check(SUCCEEDED(CALL(SetLight)(device,late_low,&b))&&SUCCEEDED(CALL(SetLight)(device,late_high,&b)),"mutate uncaptured light fields");check(SUCCEEDED(CALL(LightEnable)(device,late_low,TRUE))&&SUCCEEDED(CALL(LightEnable)(device,late_high,TRUE)),"mutate uncaptured enable");
        state(D3DRS_CULLMODE,D3DCULL_CW);state(D3DRS_TEXTUREFACTOR,0xff445566u);state(D3DRS_SHADEMODE,D3DSHADE_FLAT);state(D3DRS_SPECULARENABLE,TRUE);state(SCOPE_ORDER,3);
        check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_TEXCOORDINDEX,1)),"scope mutate coordinates");check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_TEXTURETRANSFORMFLAGS,2)),"scope mutate texture transform");check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_COLOROP,D3DTOP_ADD)),"scope mutate colour op");
        vp.Width=vp.Height=32;vp.MinZ=0;vp.MaxZ=1;check(SUCCEEDED(CALL(SetViewport)(device,&vp)),"scope mutate viewport");check(SUCCEEDED(CALL(SetMaterial)(device,&material_b)),"scope mutate material");check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_b)),"scope mutate plane");
        check(SUCCEEDED(CALL(SetLight)(device,37,&b))&&SUCCEEDED(CALL(LightEnable)(device,37,TRUE)),"scope mutate known light");
        scope_bind(buffers[1]);scope_indices(indices[1]);
#if D9WG_TNL_API == 9
        check(SUCCEEDED(CALL(SetTexture)(device,0,(IDirect3DBaseTexture9*)textures[1])),"scope mutate texture");
        check(SUCCEEDED(CALL(SetStreamSourceFreq)(device,0,0x40000003u)),"scope mutate frequency");check(SUCCEEDED(CALL(SetNPatchMode)(device,3)),"scope mutate NPatch");
        check(SUCCEEDED(CALL(SetSamplerState)(device,0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP)),"scope mutate filter");check(SUCCEEDED(CALL(SetSamplerState)(device,0,D3DSAMP_DMAPOFFSET,512)),"scope mutate DMAP");
        state(D3DRS_CCW_STENCILFUNC,D3DCMP_LESS);check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_CONSTANT,0xffa0b0c0u)),"scope mutate stage constant");
#else
        check(SUCCEEDED(CALL(SetTexture)(device,0,(IDirect3DBaseTexture8*)textures[1])),"scope mutate texture");
#endif
        scope_fvf(D3DFVF_XYZ|D3DFVF_DIFFUSE);scope_matrix_set(TRUE);light_block_apply(block);
        scope_rs(D3DRS_CULLMODE,vertex?D3DCULL_CCW:D3DCULL_CW);scope_rs(D3DRS_TEXTUREFACTOR,pixel?0xff112233u:0xff445566u);scope_rs(D3DRS_SHADEMODE,D3DSHADE_GOURAUD);scope_rs(D3DRS_SPECULARENABLE,vertex?FALSE:TRUE);scope_rs(SCOPE_ORDER,vertex?1:3);
        scope_tss(D3DTSS_TEXCOORDINDEX,0);scope_tss(D3DTSS_TEXTURETRANSFORMFLAGS,0);scope_tss(D3DTSS_COLOROP,pixel?D3DTOP_MODULATE:D3DTOP_ADD);
        Material got_material;float got_plane[4];check(SUCCEEDED(CALL(GetMaterial)(device,&got_material))&&got_material.Diffuse.r==(all?.125f:.75f),"material ALL-only");check(SUCCEEDED(CALL(GetViewport)(device,&got_vp))&&got_vp.Width==(all?48:32),"viewport ALL-only");check(SUCCEEDED(CALL(GetClipPlane)(device,5,got_plane)),"scope plane query");for(unsigned c=0;c<4;++c)check(got_plane[c]==(all?plane_a[c]:plane_b[c]),"plane ALL-only");
        scope_matrix_expect(!all);scope_binding_expect(buffers[all?0:1],indices[all?0:1],textures[all?0:1]);light_check_value(37,vertex?&a:&b,!vertex);light_check_value(late_low,&b,TRUE);light_check_value(late_high,&b,TRUE);
        DWORD fvf;
#if D9WG_TNL_API == 9
        check(SUCCEEDED(CALL(GetFVF)(device,&fvf)),"scope FVF query");check(SUCCEEDED(CALL(GetStreamSourceFreq)(device,0,&frequency))&&frequency==(vertex?1:0x40000003u),"frequency vertex scope");check(CALL(GetNPatchMode)(device)==(vertex?0:3),"NPatch vertex scope");
        DWORD sampler;check(SUCCEEDED(CALL(GetSamplerState)(device,0,D3DSAMP_ADDRESSU,&sampler))&&sampler==(pixel?D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP),"pixel sampler scope");check(SUCCEEDED(CALL(GetSamplerState)(device,0,D3DSAMP_DMAPOFFSET,&sampler))&&sampler==(vertex?256:512),"vertex DMAP scope");
        scope_rs(D3DRS_CCW_STENCILFUNC,all?D3DCMP_ALWAYS:D3DCMP_LESS);scope_tss(D3DTSS_CONSTANT,all?0xff010203u:0xffa0b0c0u);
#else
        check(SUCCEEDED(CALL(GetVertexShader)(device,&fvf)),"scope FVF query");
#endif
        check(fvf==(vertex?D3DFVF_XYZ:D3DFVF_XYZ|D3DFVF_DIFFUSE),"vertex declaration scope");
        light_block_release(block);check(SUCCEEDED(CALL(LightEnable)(device,late_low,FALSE))&&SUCCEEDED(CALL(LightEnable)(device,late_high,FALSE)),"clear late lights");
    }
#if D9WG_TNL_API == 9
    check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_a))&&SUCCEEDED(CALL(SetStreamSourceFreq)(device,0,1))&&SUCCEEDED(CALL(SetNPatchMode)(device,0)),"recording scope baseline");scope_bind(buffers[0]);
    check(SUCCEEDED(CALL(BeginStateBlock)(device)),"record separate frequency begin");check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_b))&&SUCCEEDED(CALL(SetStreamSourceFreq)(device,0,0x40000003u))&&SUCCEEDED(CALL(SetNPatchMode)(device,3)),"record separate frequency/plane/NPatch");LightBlock recorded;check(SUCCEEDED(CALL(EndStateBlock)(device,&recorded)),"record separate frequency end");
    float got_plane[4];UINT frequency;check(SUCCEEDED(CALL(GetClipPlane)(device,5,got_plane))&&got_plane[3]==2,"plane restored after recording");check(SUCCEEDED(CALL(GetStreamSourceFreq)(device,0,&frequency))&&frequency==1&&CALL(GetNPatchMode)(device)==0,"frequency and NPatch restored");scope_bind(buffers[1]);light_block_apply(recorded);
    Buffer *got;UINT offset,stride;check(SUCCEEDED(CALL(GetStreamSource)(device,0,&got,&offset,&stride))&&got==buffers[1],"frequency recording does not capture VB");BUF(Release)(got);check(SUCCEEDED(CALL(GetClipPlane)(device,5,got_plane))&&got_plane[3]==3,"recorded plane applied");check(SUCCEEDED(CALL(GetStreamSourceFreq)(device,0,&frequency))&&frequency==0x40000003u&&CALL(GetNPatchMode)(device)==3,"recorded frequency and NPatch applied");light_block_release(recorded);
#endif
    light_block_apply(baseline);light_block_release(baseline);
    for(unsigned i=0;i<2;++i){BUF(Release)(buffers[i]);
#if D9WG_TNL_API == 9
        IDirect3DIndexBuffer9_Release(indices[i]);IDirect3DTexture9_Release(textures[i]);
#else
        IDirect3DIndexBuffer8_Release(indices[i]);IDirect3DTexture8_Release(textures[i]);
#endif
    }
    report("PASS shared D3D8/9 state scope checks: independent classes and intersections, ALL-only objects/material/planes, fixed light keys and all 266 matrices");
}
#undef SCOPE_ORDER
