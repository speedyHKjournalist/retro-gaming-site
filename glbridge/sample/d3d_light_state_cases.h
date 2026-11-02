/* Independent public COM light-state checks. Defaults follow the Microsoft
 * LightEnable table; no production state helper is included in this probe. */
#if D9WG_TNL_API == 9
typedef IDirect3DStateBlock9 *LightBlock;
static LightBlock light_block_create(D3DSTATEBLOCKTYPE type) {LightBlock block;check(SUCCEEDED(CALL(CreateStateBlock)(device,type,&block)),"light block create");return block;}
static void light_block_apply(LightBlock block) {check(SUCCEEDED(IDirect3DStateBlock9_Apply(block)),"light block apply");}
static void light_block_capture(LightBlock block) {check(SUCCEEDED(IDirect3DStateBlock9_Capture(block)),"light block capture");}
static void light_block_release(LightBlock block) {IDirect3DStateBlock9_Release(block);}
#else
typedef DWORD LightBlock;
static LightBlock light_block_create(D3DSTATEBLOCKTYPE type) {LightBlock block;check(SUCCEEDED(CALL(CreateStateBlock)(device,type,&block)),"light block create");return block;}
static void light_block_apply(LightBlock block) {check(SUCCEEDED(CALL(ApplyStateBlock)(device,block)),"light block apply");}
static void light_block_capture(LightBlock block) {check(SUCCEEDED(CALL(CaptureStateBlock)(device,block)),"light block capture");}
static void light_block_release(LightBlock block) {check(SUCCEEDED(CALL(DeleteStateBlock)(device,block)),"light block delete");}
#endif
static void light_check_value(DWORD index,const Light *expected,BOOL active)
{
    Light got;BOOL enabled;check(SUCCEEDED(CALL(GetLight)(device,index,&got)),"get sparse light");
    const BYTE *a=(const BYTE*)&got,*b=(const BYTE*)expected;for(unsigned i=0;i<sizeof(got);++i)if(a[i]!=b[i]){char text[160];wsprintfA(text,"light mismatch index=%lu byte=%u got=%u expected=%u",index,i,a[i],b[i]);report(text);check(FALSE,"exact SDK light fields");}
    check(SUCCEEDED(CALL(GetLightEnable)(device,index,&enabled)),"get sparse light enable");check(!!enabled==!!active,"sparse enable state");
}
static void light_state_checks(void)
{
    const DWORD indices[]={0,7,8,37,1000,0xffffffffu};
    Light standard,custom,other;ZeroMemory(&standard,sizeof(standard));standard.Type=D3DLIGHT_DIRECTIONAL;standard.Direction.z=1;standard.Diffuse.r=standard.Diffuse.g=standard.Diffuse.b=1;
    custom=standard;custom.Diffuse.r=.25f;custom.Direction.z=-1;other=custom;other.Diffuse.g=.5f;
    for(unsigned row=0;row<sizeof(indices)/sizeof(indices[0]);++row){
        DWORD index=indices[row];Light sentinel;FillMemory(&sentinel,sizeof(sentinel),0xcc);
        check(FAILED(CALL(GetLight)(device,index,&sentinel)),"unset light rejected");for(unsigned i=0;i<sizeof(sentinel);++i)check(((BYTE*)&sentinel)[i]==0xcc,"failed light query atomic");
        check(SUCCEEDED(CALL(LightEnable)(device,index,FALSE)),"disable creates default light");light_check_value(index,&standard,FALSE);
        check(SUCCEEDED(CALL(LightEnable)(device,index,7)),"enable default light");light_check_value(index,&standard,TRUE);
        check(SUCCEEDED(CALL(SetLight)(device,index,&custom)),"replace sparse light");light_check_value(index,&custom,TRUE);
        check(SUCCEEDED(CALL(LightEnable)(device,index,FALSE)),"disable sparse light");light_check_value(index,&custom,FALSE);
    }
    LightBlock block=light_block_create(D3DSBT_VERTEXSTATE);
    check(SUCCEEDED(CALL(SetLight)(device,37,&other)),"mutate captured light");check(SUCCEEDED(CALL(LightEnable)(device,37,TRUE)),"mutate captured enable");
    light_block_apply(block);light_check_value(37,&custom,FALSE);
    check(SUCCEEDED(CALL(SetLight)(device,37,&other)),"capture changed light");check(SUCCEEDED(CALL(LightEnable)(device,37,TRUE)),"capture changed enable");light_block_capture(block);
    check(SUCCEEDED(CALL(SetLight)(device,37,&custom)),"mutate recaptured light");check(SUCCEEDED(CALL(LightEnable)(device,37,FALSE)),"mutate recaptured enable");light_block_apply(block);light_check_value(37,&other,TRUE);light_block_release(block);
    block=light_block_create(D3DSBT_PIXELSTATE);check(SUCCEEDED(CALL(SetLight)(device,37,&custom)),"pixel block light mutation");light_block_apply(block);light_check_value(37,&custom,TRUE);light_block_release(block);
    report("PASS sparse light defaults and vertex/pixel captures");
#if D9WG_TNL_API != 7
    check(SUCCEEDED(CALL(BeginStateBlock)(device)),"record sparse light begin");check(SUCCEEDED(CALL(SetLight)(device,37,&other)),"record sparse light");check(SUCCEEDED(CALL(LightEnable)(device,37,FALSE)),"record sparse enable");
#if D9WG_TNL_API == 8
    light_check_value(37,&custom,TRUE);
#endif
    check(SUCCEEDED(CALL(EndStateBlock)(device,&block)),"record sparse light end");light_check_value(37,&custom,TRUE);light_block_apply(block);light_check_value(37,&other,FALSE);light_block_release(block);
    check(SUCCEEDED(CALL(BeginStateBlock)(device)),"record enable-only begin");check(SUCCEEDED(CALL(LightEnable)(device,37,TRUE)),"record enable-only");check(SUCCEEDED(CALL(EndStateBlock)(device,&block)),"record enable-only end");
    check(SUCCEEDED(CALL(SetLight)(device,37,&custom)),"change unrecorded light fields");light_block_apply(block);light_check_value(37,&custom,TRUE);light_block_release(block);
#if D9WG_TNL_API == 9
    report("PASS D3D9 sparse recorded block restoration and masks");
#else
    report("PASS D3D8 sparse recorded block restoration and masks");
#endif
#endif
    check(SUCCEEDED(CALL(LightEnable)(device,37,FALSE)),"restore light test state");
    report("PASS sparse light state: default creation on disable, exact SDK fields, DWORD indices, enable persistence, vertex/pixel blocks and capture masks");
}
