"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),legacy=fs.readFileSync(process.env.DDRAW_MATERIAL_SOURCE||root+"/ddrawproxy/d3d_legacy_proxy.inc","utf8"),surface=fs.readFileSync(root+"/ddrawproxy/ddraw_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-material-"));
function extract(s,name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(s);assert(m,name);let end=s.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(s[end]==="{")depth++;if(s[end]==="}")depth--;}return s.slice(m.index,end);}
const program=String.raw`
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int32_t LONG,HRESULT;typedef uint32_t ULONG,DWORD;typedef int BOOL;typedef size_t SIZE_T;typedef unsigned char BYTE;
typedef void IDirectDraw7,IDirect3D,IDirect3D2,IDirect3D3,IDirect3DMaterial,IDirect3DMaterial2,IDirect3DMaterial3,IUnknown;
typedef int IDirect3DMaterialVtbl,IDirect3DMaterial2Vtbl,IDirect3DMaterial3Vtbl;
typedef struct {DWORD dwSize;struct{float r,g,b,a;}diffuse;}D3DMATERIAL;
typedef struct D3DLegacyMaterial D3DLegacyMaterial;
typedef struct{int vtbl7;LONG ref,lifetime_ref;D3DLegacyMaterial*legacy_materials;}DDrawObject;
typedef struct D3DLegacyMaterialHandle{struct D3DLegacyMaterialHandle*next;}D3DLegacyMaterialHandle;
`+legacy.match(/struct D3DLegacyMaterial \{[\s\S]*?\n};/)[0]+String.raw`
#define WINAPI
#define FALSE 0
#define D3D_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_OUTOFMEMORY (-2)
#define CLASS_E_NOAGGREGATION (-3)
#define DDWG_TRACE_ERROR(h) (h)
#define SUCCEEDED(h) ((h)>=0)
static const int g_d3d_material1_vtbl=1,g_d3d_material2_vtbl=2,g_d3d_material3_vtbl=3;
static unsigned checks,allocations,fail_alloc,owner_destroyed;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"material lifetime line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){return --*p;}
static void*heap_alloc_zero(size_t n){if(fail_alloc)return NULL;void*p=calloc(1,n);MUST(p);allocations++;return p;}
static void heap_free(void*p){if(p){MUST(allocations);allocations--;free(p);}}
static DDrawObject*d3d1_object_from_iface(void*p){return p;}static DDrawObject*d3d2_object_from_iface(void*p){return p;}static DDrawObject*d3d3_object_from_iface(void*p){return p;}
static ULONG ddraw_AddRef(IDirectDraw7*p){DDrawObject*o=p;return ++o->ref;}
static void ddraw_lifetime_release(DDrawObject*o){MUST(o->lifetime_ref>0);if(!--o->lifetime_ref){MUST(!o->ref&&!o->legacy_materials);owner_destroyed++;heap_free(o);}}
static ULONG ddraw_Release(IDirectDraw7*p){DDrawObject*o=p;MUST(o->ref>0);ULONG n=--o->ref;if(!n)ddraw_lifetime_release(o);return n;}
`+['ddraw_child_attach','ddraw_child_detach'].map(n=>extract(surface,n)).join('\n')+'\n'+['d3d_material_from_any','d3d_material_AddRef','d3d_material_Release','d3d_legacy_create_material','d3d1_CreateMaterial','d3d2_CreateMaterial','d3d3_CreateMaterial'].map(n=>extract(legacy,n)).join('\n')+String.raw`
static HRESULT create(unsigned api,DDrawObject*o,void**out,IUnknown*outer){if(api==1)return d3d1_CreateMaterial(o,out,outer);if(api==2)return d3d2_CreateMaterial(o,out,outer);return d3d3_CreateMaterial(o,out,outer);}
static void cases(unsigned api){DDrawObject*o=heap_alloc_zero(sizeof(*o));o->ref=o->lifetime_ref=1;void*p=(void*)(uintptr_t)0x1234;
    MUST(create(api,o,NULL,NULL)==DDERR_INVALIDPARAMS&&o->ref==1&&o->lifetime_ref==1);
    MUST(create(api,o,&p,(void*)1)==CLASS_E_NOAGGREGATION&&!p&&o->ref==1&&o->lifetime_ref==1);
    fail_alloc=1;p=(void*)1;MUST(create(api,o,&p,NULL)==DDERR_OUTOFMEMORY&&!p&&!o->legacy_materials&&o->lifetime_ref==1);fail_alloc=0;
    MUST(!create(api,o,&p,NULL)&&p&&o->ref==1&&o->lifetime_ref==2&&allocations==2);
    D3DLegacyMaterial*m=d3d_material_from_any(p);MUST(m==o->legacy_materials&&m->owner==o&&m->ref==1&&m->material.dwSize==sizeof(m->material)&&m->material.diffuse.r==1&&m->material.diffuse.a==1);
    MUST(d3d_material_AddRef(p)==2&&o->ref==1&&o->lifetime_ref==2);
    MUST(d3d_material_Release(p)==1&&o->ref==1&&o->lifetime_ref==2);
    unsigned old=owner_destroyed;MUST(!ddraw_Release(o)&&owner_destroyed==old&&o->ref==0&&o->lifetime_ref==1);
    MUST(m->owner==o&&d3d_material_AddRef(p)==2&&d3d_material_Release(p)==1);
    MUST(!d3d_material_Release(p)&&owner_destroyed==old+1&&!allocations);
}
int main(void){for(unsigned r=0;r<64;r++)for(unsigned api=1;api<=3;api++)cases(api);printf("PASS actual legacy material lifetime C: %u checks, all 3 factory views, private owner retention after public zero, final teardown and creation/aggregation failure unwinds\n",checks);return 0;}
`;
try{fs.writeFileSync(dir+"/test.c",program);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",dir+"/test.c","-o",dir+"/test"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/test",[],{encoding:"utf8"});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
