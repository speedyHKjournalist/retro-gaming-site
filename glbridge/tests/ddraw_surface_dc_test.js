"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_DC_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8").replace(/\\\n/g,"\n"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-dc-"));
function extract(name){const m=new RegExp("(?:static )?(?:HRESULT|void|HDC) [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/\bWINAPI\b/g,"");}
const legacy=[3,4].map(v=>extract('tag##_GetSurfaceFromDC').replaceAll('tag##','api'+v).replace(/\bIFACE\b/g,'IDirectDraw7').replace(/\bSURFIFACE\b/g,'IDirectDrawSurface7')).join('\n');
const program=String.raw`
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef void*HDC;typedef void IDirectDraw7,IDirectDrawSurface7,IDirectDrawSurface;typedef IDirectDrawSurface7*LPDIRECTDRAWSURFACE7;
typedef struct DDSurface DDSurface;typedef struct DDrawObject{DDSurface*surfaces;struct DDrawObject*next;}DDrawObject;
typedef struct{HDC screen;}DDrawNativeDC;
struct DDSurface{void*view[8];LONG ref,public_refs[8];HDC dc,lookup_screen_dc;DDrawNativeDC*gdi_dc;DDSurface*next;BOOL dead;};
#define DD_VERSION_1 1
#define DD_VERSION_7 7
#define DD_OK 0
#define E_INVALIDARG ((HRESULT)0x80070057u)
#define DDERR_NOTFOUND ((HRESULT)0x887600ffu)
#define DDERR_GENERIC ((HRESULT)0x80004005u)
#define DDWG_TRACE_ERROR(h) (h)
static unsigned checks,creates,releases,fail_dc;static DDrawObject*g_objects;static int g_object_lock;static DDSurface*release_during_create;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"surface DC line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void EnterCriticalSection(int*p){MUST(!(*p)++);}static void LeaveCriticalSection(int*p){MUST((*p)--==1);}
static DDrawObject*object_from_iface(void*p){return p;}static DDrawObject*object_from_any(void*p,void*v){(void)v;return p;}
static void*surface_interface(DDSurface*s,unsigned view){MUST(!s->dead&&(view==1||view==7));return&s->view[view];}
static ULONG surface_private_addref(DDSurface*s){MUST(!s->dead&&s->ref>0);return++s->ref;}
static ULONG surface_private_release(DDSurface*s){MUST(!s->dead&&s->ref>0);if(!--s->ref)s->dead=1;return s->ref;}
static ULONG surface_public_addref(DDSurface*s,unsigned view){surface_private_addref(s);return++s->public_refs[view];}
static ULONG surface_public_release(DDSurface*s,unsigned view){MUST(s->public_refs[view]>0);ULONG n=--s->public_refs[view];surface_private_release(s);return n;}
static HDC GetDC(void*window){MUST(!window&&!g_object_lock);creates++;if(release_during_create){DDSurface*s=release_during_create;release_during_create=NULL;surface_public_release(s,7);}return fail_dc?NULL:(HDC)(uintptr_t)0x9876;}
static BOOL ReleaseDC(void*window,HDC dc){MUST(!window&&dc==(HDC)(uintptr_t)0x9876);releases++;return 1;}
`+['surface_release_lookup_dc','surface_lookup_device_dc','ddraw_surface_from_dc','ddraw_GetSurfaceFromDC','GetSurfaceFromDC'].map(extract).join('\n')+'\n'+legacy+String.raw`
static void cases(void){DDSurface inactive={.ref=1,.public_refs={[7]=1}},native={.ref=1,.public_refs={[7]=1},.dc=(void*)111},dib={.ref=1,.public_refs={[7]=1},.dc=(void*)222};DDrawNativeDC cookie={.screen=(void*)333};native.gdi_dc=&cookie;inactive.next=&native;DDrawObject a={.surfaces=&inactive},b={.surfaces=&dib};a.next=&b;g_objects=&a;unsigned old_creates=creates,old_releases=releases;IDirectDrawSurface*out=(void*)1;HDC screen=(void*)2;
    MUST(GetSurfaceFromDC(NULL,&out,&screen)==DDERR_NOTFOUND&&!out&&!screen);screen=(void*)2;MUST(GetSurfaceFromDC(native.dc,NULL,&screen)==E_INVALIDARG&&screen==(void*)2);out=(void*)1;MUST(GetSurfaceFromDC(native.dc,&out,NULL)==E_INVALIDARG&&!out);
    MUST(!GetSurfaceFromDC(native.dc,&out,&screen)&&out==(void*)&native.view[1]&&screen==cookie.screen&&native.public_refs[1]==1&&native.public_refs[7]==1&&native.ref==2&&creates==old_creates);surface_public_release(&native,1);
    for(unsigned api=3;api<=7;api++){if(api==5||api==6)continue;out=(void*)1;HRESULT hr=api==3?api3_GetSurfaceFromDC(&a,native.dc,&out):api==4?api4_GetSurfaceFromDC(&a,native.dc,&out):ddraw_GetSurfaceFromDC(&a,native.dc,&out);unsigned view=api==7?7:1;MUST(!hr&&out==(void*)&native.view[view]&&native.public_refs[view]==(LONG)(view==7?2:1)&&native.ref==2);surface_public_release(&native,view);}
    out=(void*)1;MUST(ddraw_GetSurfaceFromDC(&a,NULL,&out)==DDERR_NOTFOUND&&!out&&inactive.ref==1);out=(void*)1;MUST(ddraw_GetSurfaceFromDC(&a,dib.dc,&out)==DDERR_NOTFOUND&&!out);MUST(ddraw_GetSurfaceFromDC(&a,native.dc,NULL)==E_INVALIDARG);
    fail_dc=1;out=(void*)1;screen=(void*)2;MUST(GetSurfaceFromDC(dib.dc,&out,&screen)==DDERR_GENERIC&&!out&&!screen&&dib.ref==1&&!dib.public_refs[1]);fail_dc=0;
    MUST(!GetSurfaceFromDC(dib.dc,&out,&screen)&&out==(void*)&dib.view[1]&&screen==(void*)0x9876&&dib.ref==2&&creates==old_creates+2);surface_public_release(&dib,1);MUST(!GetSurfaceFromDC(dib.dc,&out,&screen)&&creates==old_creates+2&&screen==dib.lookup_screen_dc);surface_public_release(&dib,1);
    surface_release_lookup_dc(&dib);surface_release_lookup_dc(&dib);MUST(releases==old_releases+1&&!dib.lookup_screen_dc);dib.dc=NULL;out=(void*)1;screen=(void*)2;MUST(GetSurfaceFromDC((void*)222,&out,&screen)==DDERR_NOTFOUND&&!out&&!screen);
    dib.dc=(void*)222;release_during_create=&dib;MUST(!GetSurfaceFromDC(dib.dc,&out,&screen)&&out==(void*)&dib.view[1]&&dib.ref==1&&!dib.dead&&!dib.public_refs[7]&&dib.public_refs[1]==1&&!release_during_create);surface_release_lookup_dc(&dib);MUST(!surface_public_release(&dib,1)&&dib.dead);MUST(native.ref==1&&inactive.ref==1&&native.public_refs[7]==1&&!native.public_refs[1]&&!g_object_lock);g_objects=NULL;
}
int main(void){(void)&surface_private_addref;for(unsigned i=0;i<128;i++)cases();printf("PASS actual surface-DC lookup C: %u checks, exported/3/4/7 views, parameter clearing order, active/NULL/unknown DCs, owner scope, native and cached display DCs, allocation failure/retry, public-reference isolation and reentrant base release\n",checks);return 0;}
`;
try{fs.writeFileSync(dir+'/test.c',program);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
