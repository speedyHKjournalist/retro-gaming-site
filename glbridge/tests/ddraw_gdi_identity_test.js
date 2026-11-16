'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_GDI_IDENTITY_SOURCE||root+'/ddrawproxy/ddraw_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-gdi-identity-'));
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,d=1;for(;d;end++){if(source[end]==='{')d++;if(source[end]==='}')d--;}return source.slice(m.index,end);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef int32_t HRESULT;typedef uint32_t DWORD;typedef unsigned char BYTE;typedef int BOOL;
#define WINAPI
#define DDWG_TRACE_ERROR(h) (h)
#define FAILED(h) ((h)<0)
#define DD_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_NOTFOUND (-2)
#define DDERR_NOTFLIPPABLE (-3)
#define DDERR_SURFACELOST (-4)
#define DDFLIP_WAIT 1
#define TRUE 1
#define FALSE 0
typedef struct{unsigned changed;}DDSurfaceMemory;typedef struct{unsigned marker;}DDTextureStorage;
typedef struct DDSurface DDSurface;typedef struct DDrawObject{DDSurface*primary;DDSurfaceMemory*gdi_memory;}DDrawObject,IDirectDraw7;
struct DDSurface{const void*vtbl7;struct DDSurface*flip_next;DDSurfaceMemory*memory;DDTextureStorage*storage;BYTE*shadow;DWORD handle;BOOL external_memory,upload_pending,front_of_chain,lost;unsigned refs;};
typedef DDSurface IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;
static unsigned checks,flips;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"gdi identity line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static DDrawObject*object_from_iface(IDirectDraw7*p){return p;}
static HRESULT surface_IsLost(IDirectDrawSurface7*p){return p->lost?DDERR_SURFACELOST:0;}
static void surface_memory_changed(DDSurfaceMemory*p){p->changed++;}
static unsigned IDirectDrawSurface7_AddRef(IDirectDrawSurface7*p){return ++p->refs;}
${extract('surface_swap_flip_storage')}
static HRESULT surface_Flip(IDirectDrawSurface7*a,IDirectDrawSurface7*b,DWORD flags){MUST(flags==DDFLIP_WAIT&&a!=b);flips++;surface_swap_flip_storage(a,b);return DD_OK;}
${extract('ddraw_GetGDISurface')}
${extract('ddraw_FlipToGDISurface')}
int main(void){for(unsigned repeat=0;repeat<256;repeat++){DDrawObject owner={0};DDSurface*out=(void*)1;MUST(ddraw_GetGDISurface(&owner,&out)==DDERR_NOTFOUND&&!out);MUST(ddraw_GetGDISurface(&owner,NULL)==DDERR_INVALIDPARAMS);MUST(ddraw_FlipToGDISurface(&owner)==DDERR_NOTFOUND);
 DDSurfaceMemory original={0},second={0},third={0};DDTextureStorage storage[3]={{1},{2},{3}};BYTE bytes[3]={11,22,33};DDSurface a={.memory=&original,.storage=&storage[0],.shadow=&bytes[0],.handle=10,.refs=1},b={.memory=&second,.storage=&storage[1],.shadow=&bytes[1],.handle=20,.refs=1},c={.memory=&third,.storage=&storage[2],.shadow=&bytes[2],.handle=30,.refs=1};owner.primary=&a;owner.gdi_memory=&original;MUST(!ddraw_GetGDISurface(&owner,&out)&&out==&a&&a.refs==2);a.refs--;MUST(ddraw_FlipToGDISurface(&owner)==DDERR_NOTFLIPPABLE);a.front_of_chain=TRUE;a.flip_next=&b;b.flip_next=&c;c.flip_next=&a;
 unsigned before=flips;MUST(!ddraw_FlipToGDISurface(&owner)&&flips==before);surface_swap_flip_storage(&a,&b);MUST(!ddraw_GetGDISurface(&owner,&out)&&out==&b&&b.refs==2&&out->shadow==&bytes[0]&&out->handle==10);b.refs--;MUST(!ddraw_FlipToGDISurface(&owner)&&flips==before+1&&a.memory==&original&&a.handle==10&&a.shadow==&bytes[0]);MUST(!ddraw_GetGDISurface(&owner,&out)&&out==&a&&a.refs==2);a.refs--;
 surface_swap_flip_storage(&a,&c);MUST(!ddraw_GetGDISurface(&owner,&out)&&out==&c&&c.refs==2);c.refs--;MUST(!ddraw_FlipToGDISurface(&owner)&&flips==before+2&&a.memory==&original);a.lost=TRUE;MUST(ddraw_FlipToGDISurface(&owner)==DDERR_SURFACELOST&&flips==before+2);a.lost=FALSE;MUST(a.refs==1&&b.refs==1&&c.refs==1&&original.changed&&second.changed&&third.changed);
 }printf("PASS actual GDI allocation identity C: %u checks; missing/plain/lost errors, sentinel clearing, native allocation identity through real storage swaps, three-member search, named flip back, redundant no-op and balanced references\\n",checks);return 0;}
`;
try{fs.writeFileSync(dir+'/test.c',code);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
