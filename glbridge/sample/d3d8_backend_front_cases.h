typedef struct FRVertex{float x,y,z,rhw,u,v,w;}FRVertex;
typedef struct FRContext{FRDevice*d;HMODULE module;UINT mode,width,height,accepted,rejected,words;}FRContext;
extern UINT fr_frame_index;
void fr_check(BOOL,const char*);void fr_hr(HRESULT,const char*);void fr_report(const char*);
static void fr_setup(FRContext*);
static HRESULT fr_make(FRContext*,UINT,UINT,UINT,UINT,FRSurface**,FRBase**);
static HRESULT fr_front(FRContext*,FRSurface*);
static void fr_read(FRContext*,DWORD);
static void fr_draw(FRContext*,FRBase*,UINT,BOOL,DWORD);
static void fr_clear(FRContext*c,DWORD color){fr_hr(FR_CLEAR(c->d,0,NULL,1,color,1,0),"FAIL Clear");}
static void fr_fill(FRSurface*s,UINT w,UINT h){D3DLOCKED_RECT l;fr_hr(FR_LOCK(s,&l,NULL,0),"FAIL fill lock");fr_check(l.Pitch==(INT)(w*4),"FAIL fill pitch");for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++)((DWORD*)((BYTE*)l.pBits+y*l.Pitch))[x]=0x11223344u;fr_hr(FR_UNLOCK(s),"FAIL fill unlock");}
static void fr_verify(FRContext*c,FRSurface*s,UINT w,UINT h,BOOL accepted){D3DLOCKED_RECT l;fr_hr(FR_LOCK(s,&l,NULL,16),"FAIL verify lock");fr_check(l.Pitch==(INT)(w*4),"FAIL verify pitch");for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){DWORD wanted=accepted&&x<c->width&&y<c->height?0xff336699u:0x11223344u;fr_check(((DWORD*)((BYTE*)l.pBits+y*l.Pitch))[x]==wanted,"FAIL front bytes/exterior/alpha");c->words++;}fr_hr(FR_UNLOCK(s),"FAIL verify unlock");}
static void fr_case(FRContext*c,UINT index){UINT kind=index==4?1:index==5?2:0,fmt=index==2||index==3?22:21,w=index==0||index==2?8:10,h=w;if(index==6){w=7;h=5;}FRSurface*s=NULL;FRBase*parent=NULL;fr_hr(fr_make(c,kind,fmt,w,h,&s,&parent),"FAIL destination");fr_fill(s,w,h);fr_clear(c,0x00336699u);BOOL accepted=c->mode!=2||index==0;HRESULT hr=fr_front(c,s);fr_check(hr==(accepted?D3D_OK:D3DERR_INVALIDCALL),"FAIL front HRESULT");if(accepted)c->accepted++;else c->rejected++;fr_verify(c,s,w,h,accepted);char line[200];wsprintfA(line,"FRONTCORE CASE mode=%u case=%u accepted=%u kind=%u format=%u width=%u height=%u preserved=1",c->mode,index,accepted,kind,fmt,w,h);fr_report(line);
 if(parent){for(UINT outside=0;outside<2;outside++){DWORD color=accepted&&!outside?0xff336699u:0x11223344u;fr_draw(c,parent,kind,outside,color);wsprintfA(line,"FRONTCORE FRAME index=%u mode=%u kind=%u sample=%u color=%08lX readback=1",fr_frame_index++,c->mode,kind,outside,color);fr_report(line);}}
 fr_check(FR_SREL(s)==(parent?1u:0u),"FAIL surface final refs");if(parent)fr_check(FR_BREL(parent)==0,"FAIL parent final refs");}
static void fr_cases(FRContext*c){fr_setup(c);for(UINT i=0;i<6;i++)fr_case(c,i);}
static void fr_post_reset(FRContext*c){c->width=5;c->height=3;fr_case(c,6);}
static void fr_summary(FRContext*c){fr_check(c->words==819&&c->accepted==(c->mode==2?1u:7u)&&c->rejected==(c->mode==2?6u:0u),"FAIL front totals");char line[200];wsprintfA(line,"FRONTCORE CHECK mode=%u accepted=%u rejected=%u localWords=819 frames=4 reset=1 finalRefs=0",c->mode,c->accepted,c->rejected);fr_report(line);}
