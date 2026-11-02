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
