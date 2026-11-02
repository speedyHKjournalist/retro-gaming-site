/* Independently authored COM checks for deferred D3D8 state recording. */
static void recording_barrier(void)
{
#ifdef D9WG_RECORDING_WIRE_BARRIERS
    check(SUCCEEDED(CALL(Present)(device,NULL,NULL,NULL,NULL)),"recording wire barrier");
#endif
}
static void d3d8_record_state_checks(void)
{
    DWORD baseline;check(SUCCEEDED(CALL(CreateStateBlock)(device,D3DSBT_ALL,&baseline)),"recording baseline");
    DWORD old_state,old_tss,old_shader;D3DVIEWPORT8 old_vp;Material old_material;D3DMATRIX old_world;
    check(SUCCEEDED(CALL(GetRenderState)(device,D3DRS_LIGHTING,&old_state)),"old recording render state");
    check(SUCCEEDED(CALL(GetTextureStageState)(device,0,D3DTSS_COLOROP,&old_tss)),"old recording stage state");
    check(SUCCEEDED(CALL(GetViewport)(device,&old_vp)),"old recording viewport");
    check(SUCCEEDED(CALL(GetMaterial)(device,&old_material)),"old recording material");
    check(SUCCEEDED(CALL(GetTransform)(device,D3DTS_WORLD,&old_world)),"old recording transform");
    check(SUCCEEDED(CALL(GetVertexShader)(device,&old_shader)),"old recording shader");
    Buffer *other;IDirect3DIndexBuffer8 *indices;IDirect3DTexture8 *texture;
    check(SUCCEEDED(CALL(CreateVertexBuffer)(device,64,0,D3DFVF_XYZ,D3DPOOL_SYSTEMMEM,&other)),"recording buffer");
    check(SUCCEEDED(CALL(CreateIndexBuffer)(device,32,0,D3DFMT_INDEX16,D3DPOOL_SYSTEMMEM,&indices)),"recording indices");
    check(SUCCEEDED(CALL(CreateTexture)(device,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture)),"recording texture");
    D3DMATRIX matrix,scale,got_matrix;identity(&matrix);matrix._11=2;matrix._22=3;matrix._41=4;identity(&scale);scale._11=.5f;scale._22=2;
    Material material=old_material;material.Diffuse.r=.125f;material.Diffuse.g=.375f;
    D3DVIEWPORT8 vp={0,0,32,32,.25f,.75f},got_vp;Material got_material;
    DWORD block,value;Buffer *got_buffer;UINT stride,base;IDirect3DIndexBuffer8 *got_indices;IDirect3DBaseTexture8 *got_texture;
    recording_barrier();
    check(SUCCEEDED(CALL(BeginStateBlock)(device)),"deferred recording begin");
    check(SUCCEEDED(CALL(SetRenderState)(device,D3DRS_LIGHTING,old_state)),"record same value");
    check(SUCCEEDED(CALL(SetRenderState)(device,D3DRS_LIGHTING,!old_state)),"record changed value");
    check(SUCCEEDED(CALL(SetTextureStageState)(device,0,D3DTSS_COLOROP,D3DTOP_ADD)),"record stage");
    check(SUCCEEDED(CALL(SetViewport)(device,&vp)),"record viewport");check(SUCCEEDED(CALL(SetMaterial)(device,&material)),"record material");
    check(SUCCEEDED(CALL(SetTransform)(device,D3DTS_WORLD,&matrix)),"record transform");check(SUCCEEDED(CALL(MultiplyTransform)(device,D3DTS_WORLD,&scale)),"record multiplied transform");
    check(SUCCEEDED(CALL(SetStreamSource)(device,0,other,12)),"record stream");check(SUCCEEDED(CALL(SetStreamSource)(device,0,other,12)),"record same stream twice");
    check(SUCCEEDED(CALL(SetIndices)(device,indices,7)),"record indices");check(SUCCEEDED(CALL(SetTexture)(device,0,(IDirect3DBaseTexture8*)texture)),"record texture");
    check(SUCCEEDED(CALL(SetVertexShader)(device,D3DFVF_XYZ)),"record FVF");check(SUCCEEDED(CALL(SetPixelShader)(device,0)),"record pixel shader");
    check(SUCCEEDED(CALL(GetRenderState)(device,D3DRS_LIGHTING,&value))&&value==old_state,"record render state remains live");
    check(SUCCEEDED(CALL(GetTextureStageState)(device,0,D3DTSS_COLOROP,&value))&&value==old_tss,"record stage remains live");
    check(SUCCEEDED(CALL(GetViewport)(device,&got_vp))&&got_vp.Width==old_vp.Width&&got_vp.MinZ==old_vp.MinZ,"record viewport remains live");
    check(SUCCEEDED(CALL(GetMaterial)(device,&got_material))&&got_material.Diffuse.r==old_material.Diffuse.r,"record material remains live");
    check(SUCCEEDED(CALL(GetTransform)(device,D3DTS_WORLD,&got_matrix))&&got_matrix._11==old_world._11&&got_matrix._41==old_world._41,"record transform remains live");
    check(SUCCEEDED(CALL(GetStreamSource)(device,0,&got_buffer,&stride))&&got_buffer==source&&stride==sizeof(Input),"record stream remains live");BUF(Release)(got_buffer);
    check(SUCCEEDED(CALL(GetIndices)(device,&got_indices,&base))&&!got_indices&&base==0,"record indices remain live");
    check(SUCCEEDED(CALL(GetTexture)(device,0,&got_texture))&&!got_texture,"record texture remains live");
    check(SUCCEEDED(CALL(GetVertexShader)(device,&value))&&value==old_shader,"record shader remains live");
    check(SUCCEEDED(CALL(EndStateBlock)(device,&block)),"deferred recording end");recording_barrier();
    check(BUF(Release)(other)==1,"recorded block retains exactly one VB reference");
    check(IDirect3DIndexBuffer8_Release(indices)==1,"recorded block retains exactly one IB reference");
    check(IDirect3DTexture8_Release(texture)==1,"recorded block retains exactly one texture reference");
    check(SUCCEEDED(CALL(ApplyStateBlock)(device,block)),"apply deferred state");recording_barrier();
    check(SUCCEEDED(CALL(GetRenderState)(device,D3DRS_LIGHTING,&value))&&value==!old_state,"applied render state");
    check(SUCCEEDED(CALL(GetTextureStageState)(device,0,D3DTSS_COLOROP,&value))&&value==D3DTOP_ADD,"applied stage");
    check(SUCCEEDED(CALL(GetViewport)(device,&got_vp))&&got_vp.Width==32&&got_vp.MinZ==.25f,"applied viewport");
    check(SUCCEEDED(CALL(GetMaterial)(device,&got_material))&&got_material.Diffuse.r==.125f,"applied material");
    check(SUCCEEDED(CALL(GetTransform)(device,D3DTS_WORLD,&got_matrix))&&got_matrix._11==1&&got_matrix._22==6&&got_matrix._41==2,"recorded matrix composition");
    check(SUCCEEDED(CALL(GetStreamSource)(device,0,&got_buffer,&stride))&&got_buffer==other&&stride==12,"applied retained stream");BUF(Release)(got_buffer);
    check(SUCCEEDED(CALL(GetIndices)(device,&got_indices,&base))&&got_indices==indices&&base==7,"applied retained indices");IDirect3DIndexBuffer8_Release(got_indices);
    check(SUCCEEDED(CALL(GetTexture)(device,0,&got_texture))&&got_texture==(IDirect3DBaseTexture8*)texture,"applied retained texture");IDirect3DBaseTexture8_Release(got_texture);
    check(SUCCEEDED(CALL(GetVertexShader)(device,&value))&&value==D3DFVF_XYZ,"applied FVF");
    check(SUCCEEDED(CALL(DeleteStateBlock)(device,block)),"delete applied recording");
    check(SUCCEEDED(CALL(ApplyStateBlock)(device,baseline)),"restore recording baseline");check(SUCCEEDED(CALL(DeleteStateBlock)(device,baseline)),"delete recording baseline");
    check(SUCCEEDED(CALL(BeginStateBlock)(device)),"same-value recording begin");check(SUCCEEDED(CALL(SetRenderState)(device,D3DRS_LIGHTING,old_state)),"record same-value state");check(SUCCEEDED(CALL(EndStateBlock)(device,&block)),"same-value recording end");
    check(SUCCEEDED(CALL(SetRenderState)(device,D3DRS_LIGHTING,!old_state)),"mutate before same-value apply");check(SUCCEEDED(CALL(ApplyStateBlock)(device,block)),"same-value apply");
    check(SUCCEEDED(CALL(GetRenderState)(device,D3DRS_LIGHTING,&value))&&value==old_state,"same-value writes remain in recording");check(SUCCEEDED(CALL(DeleteStateBlock)(device,block)),"same-value recording delete");
    report("PASS D3D8 deferred recording: live queries, render/stage/viewport/material/transform/shaders, exact VB/IB/texture references, ordered local multiplication, same-value masks and explicit Apply");
}
static void record_register_expect(BOOL pixel,DWORD reg,const float *expected)
{
    float got[4];HRESULT hr=pixel?CALL(GetPixelShaderConstant)(device,reg,got,1):CALL(GetVertexShaderConstant)(device,reg,got,1);
    check(SUCCEEDED(hr),"read recorded register");for(unsigned c=0;c<4;++c)check(got[c]==expected[c],"recorded register value");
}
static void record_plane_expect(DWORD index,const float *expected)
{
    float got[4];check(SUCCEEDED(CALL(GetClipPlane)(device,index,got)),"read recorded plane");for(unsigned c=0;c<4;++c)check(got[c]==expected[c],"recorded plane value");
}
static void d3d8_record_constant_checks(void)
{
    const float a[2][4]={{.125f,.25f,.5f,.75f},{-.125f,-.25f,-.5f,1}},b[2][4]={{.375f,.625f,.875f,1},{-.375f,-.625f,-.875f,0}};
    const float plane_a[4]={1,0,0,.25f},plane_b[4]={1,-2,.5f,3};float old_vs[2][4],old_ps[2][4],old_plane[4];DWORD baseline,block,scope;
    check(SUCCEEDED(CALL(GetVertexShaderConstant)(device,94,old_vs,2)),"original vertex register tail");check(SUCCEEDED(CALL(GetPixelShaderConstant)(device,6,old_ps,2)),"original pixel register tail");check(SUCCEEDED(CALL(GetClipPlane)(device,5,old_plane)),"original plane tail");
    check(SUCCEEDED(CALL(CreateStateBlock)(device,D3DSBT_ALL,&baseline)),"constant ALL baseline");
    check(SUCCEEDED(CALL(SetVertexShaderConstant)(device,94,a,2)),"initial vertex registers");check(SUCCEEDED(CALL(SetPixelShaderConstant)(device,6,a,2)),"initial pixel registers");check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_a)),"initial plane");
    recording_barrier();check(SUCCEEDED(CALL(BeginStateBlock)(device)),"constant recording begin");
    check(SUCCEEDED(CALL(SetVertexShaderConstant)(device,94,b,2)),"record vertex tail");check(SUCCEEDED(CALL(SetVertexShaderConstant)(device,95,a,1)),"overlap vertex tail");
    check(SUCCEEDED(CALL(SetPixelShaderConstant)(device,6,b,2)),"record pixel tail");check(SUCCEEDED(CALL(SetPixelShaderConstant)(device,7,a,1)),"overlap pixel tail");check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_b)),"record plane tail");
    check(SUCCEEDED(CALL(SetVertexShaderConstant)(device,0xffffffffu,NULL,0)),"zero vertex write");check(SUCCEEDED(CALL(SetPixelShaderConstant)(device,0xffffffffu,NULL,0)),"zero pixel write");
    check(FAILED(CALL(SetVertexShaderConstant)(device,95,a,2)),"overflow vertex write refused");check(FAILED(CALL(SetPixelShaderConstant)(device,7,a,2)),"overflow pixel write refused");check(FAILED(CALL(SetClipPlane)(device,6,plane_b)),"overflow plane refused");
    record_register_expect(FALSE,94,a[0]);record_register_expect(FALSE,95,a[1]);record_register_expect(TRUE,6,a[0]);record_register_expect(TRUE,7,a[1]);record_plane_expect(5,plane_a);
    check(SUCCEEDED(CALL(EndStateBlock)(device,&block)),"constant recording end");recording_barrier();
    record_register_expect(FALSE,94,a[0]);record_register_expect(TRUE,6,a[0]);record_plane_expect(5,plane_a);
    check(SUCCEEDED(CALL(ApplyStateBlock)(device,block)),"constant recording apply");recording_barrier();
    record_register_expect(FALSE,94,b[0]);record_register_expect(FALSE,95,a[0]);record_register_expect(TRUE,6,b[0]);record_register_expect(TRUE,7,a[0]);record_plane_expect(5,plane_b);
    check(SUCCEEDED(CALL(SetVertexShaderConstant)(device,94,a,1)),"capture vertex change");check(SUCCEEDED(CALL(SetPixelShaderConstant)(device,6,a,1)),"capture pixel change");check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_a)),"capture plane change");
    check(SUCCEEDED(CALL(CaptureStateBlock)(device,block)),"capture recorded masks");
    check(SUCCEEDED(CALL(SetVertexShaderConstant)(device,94,b,1)),"mutate captured vertex");check(SUCCEEDED(CALL(SetPixelShaderConstant)(device,6,b,1)),"mutate captured pixel");check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_b)),"mutate captured plane");
    check(SUCCEEDED(CALL(SetVertexShaderConstant)(device,20,b,1)),"unrecorded vertex register");check(SUCCEEDED(CALL(SetPixelShaderConstant)(device,2,b,1)),"unrecorded pixel register");check(SUCCEEDED(CALL(SetClipPlane)(device,4,plane_b)),"unrecorded plane");
    check(SUCCEEDED(CALL(ApplyStateBlock)(device,block)),"apply recaptured masks");
    record_register_expect(FALSE,94,a[0]);record_register_expect(TRUE,6,a[0]);record_plane_expect(5,plane_a);record_register_expect(FALSE,20,b[0]);record_register_expect(TRUE,2,b[0]);record_plane_expect(4,plane_b);
    check(SUCCEEDED(CALL(DeleteStateBlock)(device,block)),"delete constant recording");
    check(SUCCEEDED(CALL(CreateStateBlock)(device,D3DSBT_VERTEXSTATE,&scope)),"vertex constant scope");
    check(SUCCEEDED(CALL(SetVertexShaderConstant)(device,94,b,1)),"vertex scope mutate VS");check(SUCCEEDED(CALL(SetPixelShaderConstant)(device,6,b,1)),"vertex scope mutate PS");check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_b)),"vertex scope mutate plane");
    check(SUCCEEDED(CALL(ApplyStateBlock)(device,scope)),"apply vertex constant scope");record_register_expect(FALSE,94,a[0]);record_register_expect(TRUE,6,b[0]);record_plane_expect(5,plane_a);check(SUCCEEDED(CALL(DeleteStateBlock)(device,scope)),"delete vertex scope");
    check(SUCCEEDED(CALL(CreateStateBlock)(device,D3DSBT_PIXELSTATE,&scope)),"pixel constant scope");
    check(SUCCEEDED(CALL(SetVertexShaderConstant)(device,94,b,1)),"pixel scope mutate VS");check(SUCCEEDED(CALL(SetPixelShaderConstant)(device,6,a,1)),"pixel scope mutate PS");check(SUCCEEDED(CALL(SetClipPlane)(device,5,plane_b)),"pixel scope mutate plane");
    check(SUCCEEDED(CALL(ApplyStateBlock)(device,scope)),"apply pixel constant scope");record_register_expect(FALSE,94,b[0]);record_register_expect(TRUE,6,b[0]);record_plane_expect(5,plane_b);check(SUCCEEDED(CALL(DeleteStateBlock)(device,scope)),"delete pixel scope");
    check(SUCCEEDED(CALL(ApplyStateBlock)(device,baseline)),"restore all register banks");check(SUCCEEDED(CALL(DeleteStateBlock)(device,baseline)),"delete register baseline");
    record_register_expect(FALSE,94,old_vs[0]);record_register_expect(FALSE,95,old_vs[1]);record_register_expect(TRUE,6,old_ps[0]);record_register_expect(TRUE,7,old_ps[1]);record_plane_expect(5,old_plane);
    report("PASS D3D8 constant and plane recording: live queries, tail/overlap/zero/overflow writes, capture masks, VS/PS/ALL scopes and explicit Apply");
}
