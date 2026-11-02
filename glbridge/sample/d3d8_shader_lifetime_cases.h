/* Public handles expire; snapshots and live bindings retain shader objects. */
static DWORD lifetime_shader_create(BOOL pixel)
{
    static const DWORD declaration[]={D3DVSD_STREAM(0),D3DVSD_REG(D3DVSDE_POSITION,D3DVSDT_FLOAT3),D3DVSD_END()};
    static const DWORD vertex[]={0xfffe0101u,1,0xc00f0000u,0x90e40000u,0xffffu};
    static const DWORD fragment[]={0xffff0101u,1,0x800f0000u,0x90e40000u,0xffffu};
    DWORD handle;if(pixel)check(SUCCEEDED(CALL(CreatePixelShader)(device,fragment,&handle)),"lifetime create pixel");else check(SUCCEEDED(CALL(CreateVertexShader)(device,declaration,vertex,&handle,0)),"lifetime create vertex");return handle;
}
static void d3d8_shader_lifetime_checks(void)
{
    DWORD baseline;check(SUCCEEDED(CALL(CreateStateBlock)(device,D3DSBT_ALL,&baseline)),"lifetime baseline");
    for(unsigned pixel=0;pixel<2;++pixel){
        HRESULT (WINAPI *set)(Device*,DWORD)=pixel?device->lpVtbl->SetPixelShader:device->lpVtbl->SetVertexShader;
        HRESULT (WINAPI *get)(Device*,DWORD*)=pixel?device->lpVtbl->GetPixelShader:device->lpVtbl->GetVertexShader;
        HRESULT (WINAPI *remove_shader)(Device*,DWORD)=pixel?device->lpVtbl->DeletePixelShader:device->lpVtbl->DeleteVertexShader;
        HRESULT (WINAPI *function)(Device*,DWORD,void*,DWORD*)=pixel?device->lpVtbl->GetPixelShaderFunction:device->lpVtbl->GetVertexShaderFunction;
        DWORD a=lifetime_shader_create(pixel),b=lifetime_shader_create(pixel),block,value,size;
        check(SUCCEEDED(set(device,a)),"lifetime set original");check(SUCCEEDED(CALL(CreateStateBlock)(device,pixel?D3DSBT_PIXELSTATE:D3DSBT_VERTEXSTATE,&block)),"lifetime capture original");check(SUCCEEDED(set(device,b)),"lifetime replace live");check(SUCCEEDED(remove_shader(device,a)),"lifetime delete captured handle");
        check(FAILED(set(device,a))&&FAILED(remove_shader(device,a)),"lifetime stale public handle rejected");size=0xccccccccu;check(FAILED(function(device,a,NULL,&size))&&size==0xccccccccu,"lifetime stale function atomic");
        check(SUCCEEDED(CALL(ApplyStateBlock)(device,block)),"lifetime apply deleted captured object");check(SUCCEEDED(get(device,&value))&&value==a,"lifetime restored expired token");check(SUCCEEDED(CALL(DeleteStateBlock)(device,block)),"lifetime delete bound snapshot");check(SUCCEEDED(get(device,&value))&&value==a,"lifetime current binding retains object");check(SUCCEEDED(set(device,0)),"lifetime release bound object");check(SUCCEEDED(remove_shader(device,b)),"lifetime release replacement");
        a=lifetime_shader_create(pixel);check(SUCCEEDED(CALL(BeginStateBlock)(device)),"lifetime record begin");check(SUCCEEDED(set(device,a))&&SUCCEEDED(set(device,a)),"lifetime record same object twice");check(SUCCEEDED(get(device,&value))&&value==0,"lifetime record live isolation");check(SUCCEEDED(CALL(EndStateBlock)(device,&block)),"lifetime record end");check(SUCCEEDED(remove_shader(device,a)),"lifetime delete recorded handle");check(SUCCEEDED(CALL(ApplyStateBlock)(device,block)),"lifetime apply retained recorded object");check(SUCCEEDED(CALL(CaptureStateBlock)(device,block)),"lifetime capture identical deleted object");check(SUCCEEDED(CALL(DeleteStateBlock)(device,block)),"lifetime delete recorded block");check(SUCCEEDED(set(device,0)),"lifetime release recorded live object");
        a=lifetime_shader_create(pixel);b=lifetime_shader_create(pixel);check(SUCCEEDED(set(device,a)),"lifetime set recapture original");check(SUCCEEDED(CALL(CreateStateBlock)(device,D3DSBT_ALL,&block)),"lifetime ALL capture");check(SUCCEEDED(set(device,b))&&SUCCEEDED(remove_shader(device,a)),"lifetime retire old snapshot");check(SUCCEEDED(CALL(CaptureStateBlock)(device,block))&&SUCCEEDED(CALL(CaptureStateBlock)(device,block)),"lifetime Capture replaces references");check(SUCCEEDED(CALL(DeleteStateBlock)(device,block)),"lifetime delete recaptured block");check(SUCCEEDED(remove_shader(device,b)),"lifetime delete bound replacement");check(SUCCEEDED(get(device,&value))&&value==0,"lifetime delete unbinds public object");
        a=lifetime_shader_create(pixel);check(SUCCEEDED(set(device,a)),"lifetime set for recorded delete");check(SUCCEEDED(CALL(BeginStateBlock)(device))&&SUCCEEDED(remove_shader(device,a)),"lifetime delete while recording");check(SUCCEEDED(get(device,&value))&&value==a,"lifetime recording delete retains live state");check(FAILED(set(device,a)),"lifetime recording deletion expires handle immediately");check(SUCCEEDED(CALL(EndStateBlock)(device,&block)),"lifetime end recorded delete");check(SUCCEEDED(CALL(ApplyStateBlock)(device,block)),"lifetime apply recorded unbind");check(SUCCEEDED(get(device,&value))&&value==0,"lifetime recorded unbind applied");check(SUCCEEDED(CALL(DeleteStateBlock)(device,block)),"lifetime delete unbind block");
    }
    check(SUCCEEDED(CALL(ApplyStateBlock)(device,baseline))&&SUCCEEDED(CALL(DeleteStateBlock)(device,baseline)),"lifetime restore baseline");
    report("PASS D3D8 shader lifetime: expired public handles, captured/recorded VS/PS objects, binding ownership, recapture, same-reference Capture, recorded deletion and final unbind");
}
