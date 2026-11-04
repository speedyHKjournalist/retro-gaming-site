/* Shared SDK8/9 behavioral probe. Each translation unit supplies typed calls. */
extern UINT rt_frame_index;
extern void rt_report(const char*);
typedef struct RTContext {RTDevice*d;HWND window;HMODULE module;UINT mode,api,sdk;BOOL baseline,core;}RTContext;
static HRESULT rt_reset_device(RTContext*c,UINT w,UINT h,DWORD flags);
static void rt_check(BOOL ok,const char*s){if(!ok){rt_report(s);ExitProcess(1);}}
static void rt_expect(HRESULT hr,const char*s){if(FAILED(hr)){char line[180];wsprintfA(line,"%s hr=%08lX",s,(DWORD)hr);rt_report(line);ExitProcess(1);}}
static void rt_setup(RTContext*c)
{
 rt_expect(RT_FVF(c->d,68),"FAIL FVF");rt_expect(RT_RS(c->d,D3DRS_ZENABLE,FALSE),"FAIL Z");rt_expect(RT_RS(c->d,D3DRS_LIGHTING,FALSE),"FAIL light");rt_expect(RT_RS(c->d,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL cull");rt_expect(RT_TSS(c->d,0,D3DTSS_COLOROP,D3DTOP_SELECTARG2),"FAIL color op");rt_expect(RT_TSS(c->d,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE),"FAIL color arg");rt_expect(RT_TSS(c->d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2),"FAIL alpha op");rt_expect(RT_TSS(c->d,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE),"FAIL alpha arg");
}
static void rt_draw(RTContext*c,UINT size,DWORD color)
{
 struct V{float x,y,z,w;DWORD color;}q[4]={{-.5f,-.5f,.5f,1,color},{size-.5f,-.5f,.5f,1,color},{-.5f,size-.5f,.5f,1,color},{size-.5f,size-.5f,.5f,1,color}},v[6];const UINT order[]={0,1,2,2,1,3};for(UINT i=0;i<6;i++)v[i]=q[order[i]];rt_expect(RT_BEGIN(c->d),"FAIL scene");rt_expect(RT_DRAW(c->d,D3DPT_TRIANGLELIST,2,v,20),"FAIL UP draw");rt_expect(RT_END(c->d),"FAIL scene end");
}

static HRESULT rt_additional(RTContext*,RTChain**);
static HRESULT rt_chain_back(RTChain*,RTSurface**);
static DWORD rt_color(UINT step,UINT x,UINT y){if(step<2)return 0xffff0000;if(step<4)return x>=2&&x<6&&y>=1&&y<5?0xff0000ff:0xffff0000;return 0xff00ff00;}
static void rt_frame(RTContext*c,UINT target,UINT step){char line[180];wsprintfA(line,"RTLOCK FRAME index=%u mode=%u api=%u sdk=%u target=%u step=%u readback=1",rt_frame_index++,c->mode,c->api,c->sdk,target,step);rt_report(line);}
static void rt_pixels(const D3DLOCKED_RECT*data,UINT step,const RECT*r){for(LONG y=r->top;y<r->bottom;y++)for(LONG x=r->left;x<r->right;x++)rt_check(((const DWORD*)((const BYTE*)data->pBits+(y-r->top)*data->Pitch))[x-r->left]==rt_color(step,x,y),"FAIL mapped pixels");}
static void rt_cases(RTContext*c)
{
 for(UINT target=0;target<2;target++){
  RTSurface*s=NULL;RTChain*chain=NULL;if(target){rt_expect(rt_additional(c,&chain),"FAIL additional");rt_expect(rt_chain_back(chain,&s),"FAIL chain back");}else rt_expect(RT_MAKE(c->d,&s),"FAIL standalone");
  rt_expect(RT_SET(c->d,s),"FAIL bind target");rt_expect(RT_CLEAR(c->d),"FAIL clear red");
  D3DSURFACE_DESC desc;rt_expect(RT_DESC(s,&desc),"FAIL target desc");rt_check(desc.Width==8&&desc.Height==8&&desc.Format==21&&(desc.Usage&D3DUSAGE_RENDERTARGET)&&desc.Pool==D3DPOOL_DEFAULT,"FAIL target metadata");
  D3DLOCKED_RECT data={123,(void*)99};const RECT whole={0,0,8,8},partial={2,1,6,5};
  if(c->mode==1){rt_check(RT_LOCK(s,&data,NULL,D3DLOCK_READONLY)==D3DERR_INVALIDCALL&&data.Pitch==123&&data.pBits==(void*)99,"FAIL old9 refusal");rt_setup(c);rt_draw(c,8,0xffff0000);rt_expect(RT_READ(c->d,s,FALSE),"FAIL old read");rt_frame(c,target,0);}
  else for(UINT step=0;step<5;step++){
   if(step==2||step==4){rt_expect(RT_READ(c->d,s,FALSE),"FAIL post-write GPU read");rt_frame(c,target,step);continue;}
   const RECT*r=step==1?&partial:NULL;rt_expect(RT_LOCK(s,&data,r,step==0?D3DLOCK_READONLY:0),"FAIL target lock");rt_check(data.pBits&&data.Pitch>=32,"FAIL mapped pitch");rt_pixels(&data,step,r?r:&whole);rt_frame(c,target,step);
   D3DLOCKED_RECT bad={123,(void*)99};rt_check(RT_LOCK(s,&bad,NULL,D3DLOCK_READONLY)==D3DERR_INVALIDCALL&&bad.Pitch==123&&bad.pBits==(void*)99,"FAIL double lock output");if(c->api!=8)rt_check(RT_READ(c->d,s,TRUE)==D3DERR_INVALIDCALL,"FAIL locked copy guard");
   if(step){const RECT*area=r?r:&whole;for(LONG y=area->top;y<area->bottom;y++)for(LONG x=area->left;x<area->right;x++)((DWORD*)((BYTE*)data.pBits+(y-area->top)*data.Pitch))[x-area->left]=step==1?0xff0000ff:0xff00ff00;}
   rt_expect(RT_UNLOCK(s),"FAIL target unlock");rt_check(RT_UNLOCK(s)==D3DERR_INVALIDCALL,"FAIL unmatched unlock");
  }
  if(c->mode>=3){rt_expect(RT_LOCK(s,&data,NULL,D3DLOCK_READONLY),"FAIL zero-public lock");rt_pixels(&data,4,&whole);rt_frame(c,target,5);if(chain){rt_check(!RT_RELEASE_CHAIN(chain),"FAIL zero chain");chain=NULL;}rt_check(!RT_RELEASE_SURFACE(s),"FAIL zero surface");rt_check(rt_reset_device(c,8,8,0)==(c->core?D3DERR_DEVICELOST:D3DERR_INVALIDCALL),"FAIL locked zero-public Reset");RTSurface*same=NULL;rt_expect(RT_GET(c->d,&same),"FAIL revive locked target");rt_check(same==s,"FAIL revived identity");rt_expect(RT_UNLOCK(same),"FAIL revived unlock");}
  RTSurface*back=NULL;rt_expect(RT_BACK(c->d,&back),"FAIL implicit restore");rt_expect(RT_SET(c->d,back),"FAIL restore target");rt_check(!RT_RELEASE_SURFACE(back),"FAIL implicit refs");rt_check(!RT_RELEASE_SURFACE(s),"FAIL target refs");if(chain)rt_check(!RT_RELEASE_CHAIN(chain),"FAIL chain refs");
  if(c->mode>=3)rt_expect(rt_reset_device(c,8,8,0),"FAIL recovered Reset");
  char line[220];wsprintfA(line,"RTLOCK CHECK mode=%u target=%u baselineRefused=%u partialFull=%u zeroPublicReset=%u finalRefs=0",c->mode,target,c->mode==1,c->mode!=1,c->mode>=3);rt_report(line);
 }
 rt_check(!RT_RELEASE_DEVICE(c->d),"FAIL device refs");
}
