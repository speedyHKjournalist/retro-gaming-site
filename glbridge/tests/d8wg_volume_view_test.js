"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-volume-view-")),source=fs.readFileSync(process.env.D8WG_BACKEND_VOLUME_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,n);let b=source.indexOf("{",m.index)+1,d=1;for(;d;b++){if(source[b]==="{")d++;if(source[b]==="}")d--;}return source.slice(m.index,b);}
try{const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
typedef uint32_t UINT;typedef int32_t HRESULT;typedef struct{const void*lpVtbl;}IDirect3DVolume9;
typedef struct Volume D9Volume;typedef struct{UINT api;}Device;typedef struct{D9Volume*level_volume;}Level;typedef struct{Device*device;UINT refs,level_count;Level levels[12];}D9VolumeTexture,IDirect3DVolumeTexture9;struct Volume{IDirect3DVolume9 iface;D9VolumeTexture*texture;UINT level;};
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-3)
#define TRACE_REFUSE(v) (v)
#define HEAP_ZERO_MEMORY 8
#define GetProcessHeap() NULL
static int g_volume_vtbl;static UINT allocations,fail_at,live;
static void*HeapAlloc(void*h,UINT flags,size_t bytes){(void)h;assert(flags==8&&bytes==sizeof(D9Volume));if(++allocations==fail_at)return NULL;void*p=calloc(1,bytes);assert(p);live++;return p;}
static UINT device_compatibility_api(Device*d){return d->api;}static D9VolumeTexture*volume_texture_from_iface(IDirect3DVolumeTexture9*t){return t;}
static UINT IDirect3DVolume9_AddRef(IDirect3DVolume9*v){D9Volume*volume=(D9Volume*)v;return ++volume->texture->refs;}
${extract('volume_texture_get_volume_level')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Volume view case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)&IDirect3DVolume9_AddRef;const UINT counts[]={0,1,2,12},indices[]={0,1,11,12,UINT32_MAX};
for(UINT api=8;api<=9;api++)for(UINT c=0;c<4;c++)for(UINT i=0;i<5;i++)for(UINT cached=0;cached<2;cached++)for(UINT failure=0;failure<2;failure++)for(UINT nullout=0;nullout<2;nullout++){
 Device d={api};D9VolumeTexture t={.device=&d,.refs=1,.level_count=counts[c]};fail_at=0;allocations=0;for(UINT level=0;level<t.level_count;level++)if(api==9||cached){D9Volume*v=HeapAlloc(NULL,8,sizeof(*v));v->iface.lpVtbl=&g_volume_vtbl;v->texture=&t;v->level=level;t.levels[level].level_volume=v;}UINT before_live=live;allocations=0;fail_at=failure;IDirect3DVolume9*out=(void*)(uintptr_t)99;UINT level=indices[i];HRESULT expected=nullout||level>=t.level_count?-1:api==8&&!cached&&failure?-3:0;CHECK(volume_texture_get_volume_level(&t,level,nullout?NULL:&out)==expected);
 if(!expected){D9Volume*v=(D9Volume*)out;CHECK(v&&v->iface.lpVtbl==&g_volume_vtbl&&v->texture==&t&&v->level==level&&t.levels[level].level_volume==v&&t.refs==2&&allocations==(api==8&&!cached));IDirect3DVolume9*again=NULL;CHECK(!volume_texture_get_volume_level(&t,level,&again)&&again==out&&t.refs==3&&allocations==(api==8&&!cached));t.refs-=2;}else{CHECK(t.refs==1&&live==before_live&&out==(nullout||api==9?(void*)(uintptr_t)99:NULL));CHECK(allocations==(expected==-3));if(level<t.level_count&&!cached&&api==8)CHECK(!t.levels[level].level_volume);}
 for(UINT n=0;n<t.level_count;n++)if(t.levels[n].level_volume){free(t.levels[n].level_volume);live--;}CHECK(!live&&t.refs==1);cases++;
}
printf("PASS actual volume level views: %u checks,%u cases;lazy8/eager9 cached identity,one shared parent AddRef per query,invalid-level output versioning,null output,allocation failure publication atomicity andteardown\\n",checks,cases);return 0;}
`;fs.writeFileSync(dir+"/view.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/view.c","-o",dir+"/view"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/view",[],{encoding:"utf8",timeout:60000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
