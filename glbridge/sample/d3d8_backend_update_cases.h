/* Native pool rule from Wine d3d8/tests/device.c:test_update_texture_pool.
 * Cube applies the same resource-pool contract, with allsix faces checked. */
typedef struct UPContext {UPDevice*d;HMODULE module;UINT mode,accepted,rejected,words;} UPContext;
void up_check(BOOL,const char*);void up_hr(HRESULT,const char*);void up_report(const char*);
static HRESULT up_make(UPContext*,UINT,UINT,UPBase**);
static HRESULT up_lock(UPBase*,UINT,UINT,DWORD,void**);
static HRESULT up_unlock(UPBase*,UINT,UINT);
static HRESULT up_update(UPContext*,UPBase*,UPBase*);
static void up_drop(UPBase*);
static void up_cases(UPContext*c)
{
    for(UINT kind=0;kind<3;kind++)for(UINT source_pool=0;source_pool<4;source_pool++)for(UINT destination_pool=0;destination_pool<4;destination_pool++){
        UPBase*source=NULL,*destination=NULL;up_hr(up_make(c,kind,source_pool,&source),"FAIL create source");up_hr(up_make(c,kind,destination_pool,&destination),"FAIL create destination");UINT faces=kind==1?6:1;
        for(UINT face=0;face<faces;face++){void*bits=NULL;up_hr(up_lock(source,kind,face,0,&bits),"FAIL source lock");*(DWORD*)bits=0x11223344u+face;up_hr(up_unlock(source,kind,face),"FAIL source unlock");up_hr(up_lock(destination,kind,face,0,&bits),"FAIL destination lock");*(DWORD*)bits=0x44332211u+face;up_hr(up_unlock(destination,kind,face),"FAIL destination unlock");}
        BOOL accepted=c->mode==0||(source_pool==2&&destination_pool==0);HRESULT hr=up_update(c,source,destination);if(hr!=(accepted?D3D_OK:D3DERR_INVALIDCALL)){char text[160];wsprintfA(text,"FAIL UpdateTexture mode=%u kind=%u source=%u destination=%u hr=%08lx",c->mode,kind,source_pool,destination_pool,(DWORD)hr);up_check(FALSE,text);}if(accepted)c->accepted++;else c->rejected++;
        for(UINT face=0;face<faces;face++){void*bits=NULL;up_hr(up_lock(destination,kind,face,16,&bits),"FAIL destination readonly");DWORD wanted=(accepted?0x11223344u:0x44332211u)+face;up_check(*(DWORD*)bits==wanted,"FAIL pool HRESULT/pixel side effect");c->words++;up_hr(up_unlock(destination,kind,face),"FAIL destination readonly unlock");}
        up_drop(source);up_drop(destination);
    }
}
static void up_log_result(const UPContext*c)
{
    up_check(c->words==128&&c->accepted==(c->mode==0?48u:3u)&&c->rejected==(c->mode==0?0u:45u),"FAIL pool totals");char text[192];wsprintfA(text,"UPCORE CHECK mode=%u poolCases=48 accepted=%u rejected=%u localWords=128 kinds=3 reset=1 finalRefs=0",c->mode,c->accepted,c->rejected);up_report(text);
}
