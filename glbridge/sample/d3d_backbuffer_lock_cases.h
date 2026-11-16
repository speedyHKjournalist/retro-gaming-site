/* Shared SDK8/9 behavioral probe. Each translation unit supplies typed calls. */
extern UINT bb_frame_index;
extern void bb_report(const char*);
typedef struct BBContext {BBDevice*d;HWND window;HMODULE module;UINT mode,api,sdk;BOOL baseline,core,msaa;}BBContext;
static HRESULT bb_reset_device(BBContext*c,UINT w,UINT h,DWORD flags);
static void bb_check(BOOL ok,const char*s){if(!ok){bb_report(s);ExitProcess(1);}}
static void bb_expect(HRESULT hr,const char*s){if(FAILED(hr)){char line[180];wsprintfA(line,"%s hr=%08lX",s,(DWORD)hr);bb_report(line);ExitProcess(1);}}
static void bb_setup(BBContext*c)
{
 bb_expect(BB_FVF(c->d,68),"FAIL FVF");bb_expect(BB_RS(c->d,D3DRS_ZENABLE,FALSE),"FAIL Z");bb_expect(BB_RS(c->d,D3DRS_LIGHTING,FALSE),"FAIL light");bb_expect(BB_RS(c->d,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL cull");bb_expect(BB_TSS(c->d,0,D3DTSS_COLOROP,D3DTOP_SELECTARG2),"FAIL color op");bb_expect(BB_TSS(c->d,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE),"FAIL color arg");bb_expect(BB_TSS(c->d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2),"FAIL alpha op");bb_expect(BB_TSS(c->d,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE),"FAIL alpha arg");
}
static void bb_draw(BBContext*c,UINT size,DWORD color)
{
 struct V{float x,y,z,w;DWORD color;}q[4]={{-.5f,-.5f,.5f,1,color},{size-.5f,-.5f,.5f,1,color},{-.5f,size-.5f,.5f,1,color},{size-.5f,size-.5f,.5f,1,color}},v[6];const UINT order[]={0,1,2,2,1,3};for(UINT i=0;i<6;i++)v[i]=q[order[i]];bb_expect(BB_BEGIN(c->d),"FAIL scene");bb_expect(BB_DRAW(c->d,D3DPT_TRIANGLELIST,2,v,20),"FAIL UP draw");bb_expect(BB_END(c->d),"FAIL scene end");
}
static void bb_frame(BBContext*c,UINT step,UINT size)
{
 char line[180];wsprintfA(line,"BBLOCK FRAME index=%u mode=%u api=%u sdk=%u step=%u size=%u readback=1",bb_frame_index++,c->mode,c->api,c->sdk,step,size);bb_report(line);
}
static DWORD bb_color(UINT step,UINT x,UINT y)
{
 if(step<=1) return 0xffff0000;
 if(step<=3) return x>=2&&x<6&&y>=1&&y<5?0xff0000ff:0xffff0000;
 return step==4?0xff00ff00:0xff0000ff;
}
static void bb_pixels(const D3DLOCKED_RECT*lock,UINT step,const RECT*r)
{
 for(LONG y=r->top;y<r->bottom;y++)for(LONG x=r->left;x<r->right;x++)bb_check(((const DWORD*)((const BYTE*)lock->pBits+(y-r->top)*lock->Pitch))[x-r->left]==bb_color(step,x,y),"FAIL lock CPU bytes");
}
static void bb_descriptor(BBSurface*s,UINT size)
{
 D3DSURFACE_DESC desc;bb_expect(BB_DESC(s,&desc),"FAIL desc");bb_check(desc.Width==size&&desc.Height==size&&desc.Format==D3DFMT_A8R8G8B8&&desc.MultiSampleType==D3DMULTISAMPLE_NONE&&desc.Pool==D3DPOOL_DEFAULT&&(desc.Usage&D3DUSAGE_RENDERTARGET),"FAIL backbuffer metadata");
}
static void bb_cases(BBContext*c)
{
 BBSurface*back=NULL,*same=NULL;bb_expect(BB_BACK(c->d,&back),"FAIL backbuffer");bb_expect(BB_BACK(c->d,&same),"FAIL cached backbuffer");bb_check(back==same&&BB_RELEASE_SURFACE(same)==1,"FAIL backbuffer identity");D3DLOCKED_RECT out={123,(void*)99};bb_check(BB_LOCK(back,&out,NULL,D3DLOCK_READONLY)==D3DERR_INVALIDCALL&&out.Pitch==123&&out.pBits==(void*)99,"FAIL initial lock refusal/output");bb_check(!BB_RELEASE_SURFACE(back),"FAIL initial surface refs");
 if(c->baseline){bb_setup(c);bb_draw(c,8,0xffff0000);bb_expect(BB_BACK(c->d,&back),"FAIL old target");bb_expect(BB_BASELINE_READ(c->d,back),"FAIL baseline read");bb_frame(c,0,8);bb_check(!BB_RELEASE_SURFACE(back)&&!BB_RELEASE_DEVICE(c->d),"FAIL old refs");char line[180];wsprintfA(line,"BBLOCK CHECK mode=%u api=%u sdk=%u baseline=1 lockableRefused=1 outputGuards=1 finalRefs=0",c->mode,c->api,c->sdk);bb_report(line);return;}
 bb_expect(bb_reset_device(c,8,8,1),"FAIL enable lockable Reset");bb_setup(c);bb_draw(c,8,0xffff0000);bb_expect(BB_BACK(c->d,&back),"FAIL lockable target");bb_descriptor(back,8);const RECT whole={0,0,8,8},partial={2,1,6,5},sub={1,2,5,6};
 for(UINT step=0;step<5;step++){
  const RECT*rect=step==1?&partial:step==4?&sub:NULL;DWORD flags=step==1||step==3?0:D3DLOCK_READONLY;out=(D3DLOCKED_RECT){123,(void*)99};bb_expect(BB_LOCK(back,&out,rect,flags),"FAIL current lock");bb_check(out.pBits&&out.Pitch>=32,"FAIL pitch");bb_pixels(&out,step,rect?rect:&whole);bb_frame(c,step,8);bb_descriptor(back,8);
  if(step==1||step==3){const RECT*r=rect?rect:&whole;for(LONG y=r->top;y<r->bottom;y++)for(LONG x=r->left;x<r->right;x++)((DWORD*)((BYTE*)out.pBits+(y-r->top)*out.Pitch))[x-r->left]=step==1?0xff0000ff:0xff00ff00;}
  if(step==4){D3DLOCKED_RECT bad={123,(void*)99};bb_check(BB_LOCK(back,&bad,NULL,D3DLOCK_READONLY)==D3DERR_INVALIDCALL&&bad.Pitch==123&&bad.pBits==(void*)99,"FAIL double lock guard");bb_check(bb_reset_device(c,8,8,1)==(c->api==8||c->core?D3DERR_DEVICELOST:D3DERR_INVALIDCALL)&&BB_COOPERATIVE(c->d)==D3DERR_DEVICENOTRESET,"FAIL locked Reset blocker");}
  bb_expect(BB_UNLOCK(back),"FAIL unlock");bb_check(BB_UNLOCK(back)==D3DERR_INVALIDCALL,"FAIL unmatched unlock");
 }
 bb_check(!BB_RELEASE_SURFACE(back),"FAIL before resize surface refs");bb_expect(bb_reset_device(c,4,4,1),"FAIL resize Reset");bb_setup(c);bb_draw(c,4,0xff0000ff);bb_expect(BB_BACK(c->d,&back),"FAIL resized backbuffer");bb_descriptor(back,4);bb_expect(BB_LOCK(back,&out,NULL,D3DLOCK_READONLY),"FAIL resized lock");const RECT small={0,0,4,4};bb_check(out.Pitch>=16,"FAIL resized pitch");bb_pixels(&out,5,&small);bb_frame(c,5,4);bb_expect(BB_UNLOCK(back),"FAIL resized unlock");bb_check(!BB_RELEASE_SURFACE(back)&&!BB_RELEASE_DEVICE(c->d),"FAIL final refs");char line[230];wsprintfA(line,"BBLOCK CHECK mode=%u api=%u sdk=%u baseline=0 noFlag=1 readonly=1 partialWrite=1 fullWrite=1 reset=2 resize=4 msaa=%u outputGuards=1 finalRefs=0",c->mode,c->api,c->sdk,c->msaa);bb_report(line);
}
