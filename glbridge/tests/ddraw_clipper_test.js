"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(process.env.DDRAW_CLIPPER_SOURCE||path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-clipper-"));
function extract(name){const m=new RegExp("(?:static )?[^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;at++){if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,UINT,ULONG;typedef int32_t LONG,HRESULT;typedef uint8_t BYTE;typedef int BOOL,WINBOOL;typedef void*HWND;typedef void IUnknown;
typedef struct{LONG left,top,right,bottom;}RECT,*LPRECT;typedef struct{LONG x,y;}POINT;
typedef struct{DWORD dwSize,iType,nCount,nRgnSize;RECT rcBound;}RGNDATAHEADER;
typedef struct{RGNDATAHEADER rdh;BYTE Buffer[1];}RGNDATA,*LPRGNDATA;typedef DWORD*LPDWORD;
typedef struct DDClipper DDClipper;typedef struct DDSurface{DDClipper*clipper;struct DDSurface*next;}DDSurface;
typedef struct DDrawObject{DDClipper*clippers;DDSurface*surfaces;struct DDrawObject*next;}DDrawObject,*LPDIRECTDRAW;
struct DDClipper{void*vtbl;LONG ref;DDrawObject*owner;HWND window;RGNDATA*clip_list;DWORD clip_list_bytes;BOOL changed;DDClipper*next,*all_next;};typedef DDClipper IDirectDrawClipper,*LPDIRECTDRAWCLIPPER;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define DD_OK 0
#define E_FAIL (-1)
#define DDERR_INVALIDPARAMS (-2)
#define DDERR_ALREADYINITIALIZED (-3)
#define DDERR_REGIONTOOSMALL (-4)
#define DDERR_GENERIC (-5)
#define DDERR_NOCLIPLIST (-6)
#define DDERR_CLIPPERISUSINGHWND (-7)
#define DDERR_OUTOFMEMORY (-8)
#define CLASS_E_NOAGGREGATION (-9)
#define RDH_RECTANGLES 1
#define DDWG_TRACE_ERROR(x) (x)
#define CopyMemory(d,s,n) memcpy(d,s,n)
#define ZeroMemory(p,n) memset(p,0,n)
#define EnterCriticalSection(p) ((void)(p))
#define LeaveCriticalSection(p) ((void)(p))
static int g_object_lock,g_clipper_vtbl;static LONG g_clipper_count;static DDClipper*g_all_clippers;static DDrawObject*g_objects;static BOOL window_live=TRUE;static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw clipper line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static void*heap_alloc_zero(size_t n){return calloc(1,n);}static void heap_free(void*p){free(p);}static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){return --*p;}
static void ddraw_child_detach(DDrawObject*d,BOOL pin){(void)d;(void)pin;}
static BOOL IsWindow(HWND h){return h==(void*)1&&window_live;}
static BOOL GetClientRect(HWND h,RECT*r){if(!IsWindow(h))return FALSE;*r=(RECT){0,0,632,453};return TRUE;}
static LONG MapWindowPoints(HWND a,HWND b,POINT*p,UINT count){(void)a;(void)b;for(UINT i=0;i<count;i++){p[i].x+=14;p[i].y+=33;}return 1;}
${["clipper_from_iface","clipper_AddRef","clipper_Release","clipper_GetClipList","clipper_GetHWnd","clipper_Initialize","clipper_IsClipListChanged","clipper_SetClipList","clipper_SetHWnd","DirectDrawCreateClipper"].map(extract).join("\n")}
int main(void){for(unsigned round=0;round<32;round++){
 DDClipper*c=NULL;CHECK(!DirectDrawCreateClipper(0,&c,NULL)&&c&&g_all_clippers==c&&g_clipper_count==1);CHECK(clipper_Initialize(c,NULL,0)==DDERR_ALREADYINITIALIZED);DWORD size=0x1234;CHECK(clipper_GetClipList(c,NULL,NULL,&size)==DDERR_NOCLIPLIST&&size==0x1234);BOOL changed=99;CHECK(!clipper_IsClipListChanged(c,&changed)&&!changed);
 struct{RGNDATAHEADER rdh;RECT rects[2];} input={{sizeof(RGNDATAHEADER),RDH_RECTANGLES,2,sizeof(RECT)*2,{1,2,40,50}},{{1,2,10,20},{20,30,40,50}}},output={0};
 CHECK(!clipper_SetClipList(c,(RGNDATA*)&input,0)&&c->clip_list!=(void*)&input);CHECK(!clipper_IsClipListChanged(c,&changed)&&changed);CHECK(!clipper_IsClipListChanged(c,&changed)&&!changed);CHECK(!clipper_GetClipList(c,NULL,NULL,&size)&&size==sizeof(input));size=1;CHECK(clipper_GetClipList(c,NULL,(RGNDATA*)&output,&size)==DDERR_REGIONTOOSMALL&&size==1);size=sizeof(output);CHECK(!clipper_GetClipList(c,NULL,(RGNDATA*)&output,&size)&&!memcmp(&input,&output,sizeof(input)));
 RGNDATA*saved=c->clip_list;CHECK(!clipper_SetHWnd(c,0,(void*)1)&&c->clip_list==saved);CHECK(clipper_SetClipList(c,(RGNDATA*)&input,0)==DDERR_CLIPPERISUSINGHWND&&c->clip_list==saved);CHECK(clipper_SetHWnd(c,1,NULL)==DDERR_INVALIDPARAMS&&c->window==(void*)1);CHECK(clipper_SetClipList(c,NULL,1)==DDERR_INVALIDPARAMS&&c->clip_list==saved);HWND window=NULL;CHECK(!clipper_GetHWnd(c,&window)&&window==(void*)1);
 CHECK(!clipper_GetClipList(c,NULL,NULL,&size)&&size==sizeof(RGNDATAHEADER)+sizeof(RECT));size=sizeof(output);CHECK(!clipper_GetClipList(c,NULL,(RGNDATA*)&output,&size));CHECK(output.rdh.nCount==1&&output.rdh.rcBound.left==14&&output.rdh.rcBound.top==33&&output.rdh.rcBound.right==646&&output.rdh.rcBound.bottom==486);
 window_live=FALSE;size=0x1234;CHECK(clipper_GetClipList(c,NULL,NULL,&size)==E_FAIL&&size==0x1234);window_live=TRUE;CHECK(!clipper_SetHWnd(c,0,NULL)&&c->clip_list==saved);size=sizeof(output);CHECK(!clipper_GetClipList(c,NULL,(RGNDATA*)&output,&size)&&!memcmp(&input,&output,sizeof(input)));CHECK(!clipper_SetClipList(c,NULL,0)&&!c->clip_list);CHECK(clipper_GetClipList(c,NULL,NULL,&size)==DDERR_NOCLIPLIST);
 DDClipper fake={.vtbl=&g_clipper_vtbl};changed=0x1234;window=(void*)2;size=0x1234;CHECK(!clipper_AddRef(&fake)&&!clipper_Release(&fake));CHECK(clipper_GetHWnd(&fake,&window)==DDERR_INVALIDPARAMS&&window==(void*)2);CHECK(clipper_IsClipListChanged((void*)(uintptr_t)0xdeadbeef,&changed)==DDERR_INVALIDPARAMS&&changed==0x1234);CHECK(clipper_GetClipList(&fake,NULL,NULL,&size)==DDERR_INVALIDPARAMS&&size==0x1234);
 c->vtbl=(void*)(uintptr_t)0xdeadbeef;CHECK(clipper_AddRef(c)==2&&!clipper_Release(c)&&c->ref==2);c->vtbl=&g_clipper_vtbl;CHECK(clipper_Release(c)==1&&!clipper_Release(c));CHECK(!g_all_clippers&&!g_clipper_count&&!clipper_AddRef(c)&&!clipper_Release(c));
 }printf("PASS actual DirectDraw Clipper C: %u checks; live/freed/fake identities, damaged-vtable counts, standalone registration, HWND screen coordinates and destruction, retained custom region and invalid-call output preservation\\n",checks);return 0;}
`;
try{fs.writeFileSync(dir+"/test.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
