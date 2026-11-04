"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'surface8-adapter-'));
const volumeMode=process.env.D8WG_TEST_VOLUME_ADAPTER==="1";
const cubeMode=process.env.D8WG_TEST_CUBE_ADAPTER==="1";
const textureMode=process.env.D8WG_TEST_TEXTURE_ADAPTER==="1";
const resourceMode=textureMode||cubeMode||volumeMode,resourceName=volumeMode?"volume_texture":cubeMode?"cube_texture":"texture",resourcePrefix=volumeMode?"VolumeTexture":cubeMode?"CubeTexture":"Texture";
const common=fs.readFileSync(process.env.D8WG_CORE_VIEW_SOURCE||root+'/d3d8proxy/d3d8_core_view.h','utf8').replace(/^#include[^\n]*\n/gm,'');
const source=fs.readFileSync(process.env.D8WG_SURFACE_ADAPTER_SOURCE||root+'/d3d8proxy/d3d8_core_surface.c','utf8').replace(/^#include[^\n]*\n/gm,''),header=fs.readFileSync(root+'/d3d9proxy/d3d9_core.h','utf8'),methods=header.match(/typedef struct D9WGSurfaceMethods8 \{[\s\S]*?\} D9WGSurfaceMethods8;/)[0];
const textureMethods=resourceMode?header.match(new RegExp("typedef struct D9WG"+resourcePrefix+"Methods8 \\{[\\s\\S]*?\\} D9WG"+resourcePrefix+"Methods8;"))[0]:"";
const volumeMethods=volumeMode?header.match(/typedef struct D9WGVolumeMethods8 \{[\s\S]*?\} D9WGVolumeMethods8;/)[0]:"";
const volumeSource=volumeMode?fs.readFileSync(process.env.D8WG_VOLUME_ADAPTER_SOURCE||root+"/d3d8proxy/d3d8_core_volume.c","utf8").replace(/^#include[^\n]*\n/gm,""):"";
const textureTypes=resourceMode?fs.readFileSync(root+'/tests/native/d8wg_'+resourceName+'_adapter_types.h','utf8'):"";
const textureSource=resourceMode?fs.readFileSync((volumeMode?process.env.D8WG_VOLUME_TEXTURE_ADAPTER_SOURCE:cubeMode?process.env.D8WG_CUBE_ADAPTER_SOURCE:process.env.D8WG_TEXTURE_ADAPTER_SOURCE)||root+'/d3d8proxy/d3d8_core_'+resourceName+'.c','utf8').replace(/^#include[^\n]*\n/gm,''):"";
try{fs.writeFileSync(dir+'/adapter.c',`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef uint32_t UINT,DWORD,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define D3D_OK 0
#define FAILED(hr) ((hr)<0)
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-2)
#define E_POINTER (-3)
#define E_NOINTERFACE (-4)
#define HEAP_ZERO_MEMORY 8
typedef struct GUID{uint32_t Data1;uint16_t Data2,Data3;uint8_t Data4[8];}GUID;
typedef const GUID*REFIID;typedef const GUID*REFGUID;
static const GUID IID_IUnknown={1,0,0,{0}},IID_IDirect3DResource8={2,0,0,{0}},IID_IDirect3DSurface8={3,0,0,{0}},IID_IDirect3DDevice8={4,0,0,{0}},IID_IDirect3DTexture8={5,0,0,{0}},bad_iid={99,0,0,{0}};
typedef struct RECT{LONG left,top,right,bottom;}RECT;
typedef struct D3DSURFACE_DESC{DWORD words[8];}D3DSURFACE_DESC;
typedef struct D3DLOCKED_RECT{LONG Pitch;void*pBits;}D3DLOCKED_RECT;
typedef struct IUnknown IUnknown;typedef struct IDirect3DSurface8 IDirect3DSurface8;typedef struct IDirect3DDevice8 IDirect3DDevice8;
typedef struct IUnknownVtbl{HRESULT(*QueryInterface)(IUnknown*,REFIID,void**);ULONG(*AddRef)(IUnknown*);ULONG(*Release)(IUnknown*);}IUnknownVtbl;
struct IUnknown{const IUnknownVtbl*lpVtbl;};struct IDirect3DSurface9{const IUnknownVtbl*lpVtbl;};struct IDirect3DDevice8{const IUnknownVtbl*lpVtbl;};
typedef struct IDirect3DSurface8Vtbl {
 HRESULT(*QueryInterface)(IDirect3DSurface8*,REFIID,void**);ULONG(*AddRef)(IDirect3DSurface8*);ULONG(*Release)(IDirect3DSurface8*);
 HRESULT(*GetDevice)(IDirect3DSurface8*,IDirect3DDevice8**);
 HRESULT(*SetPrivateData)(IDirect3DSurface8*,REFGUID,const void*,DWORD,DWORD);HRESULT(*GetPrivateData)(IDirect3DSurface8*,REFGUID,void*,DWORD*);HRESULT(*FreePrivateData)(IDirect3DSurface8*,REFGUID);
 HRESULT(*GetContainer)(IDirect3DSurface8*,REFIID,void**);HRESULT(*GetDesc)(IDirect3DSurface8*,D3DSURFACE_DESC*);HRESULT(*LockRect)(IDirect3DSurface8*,D3DLOCKED_RECT*,const RECT*,DWORD);HRESULT(*UnlockRect)(IDirect3DSurface8*);
}IDirect3DSurface8Vtbl;
struct IDirect3DSurface8{const IDirect3DSurface8Vtbl*lpVtbl;};
#define IUnknown_QueryInterface(p,i,o) ((p)->lpVtbl->QueryInterface((IUnknown*)(p),i,o))
#define IUnknown_AddRef(p) ((p)->lpVtbl->AddRef((IUnknown*)(p)))
#define IUnknown_Release(p) ((p)->lpVtbl->Release((IUnknown*)(p)))
#define IDirect3DDevice8_AddRef(p) IUnknown_AddRef(p)
#define IDirect3DDevice8_Release(p) IUnknown_Release(p)
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){assert(*p>0);return --*p;}static LONG InterlockedCompareExchange(LONG*p,LONG v,LONG c){LONG old=*p;if(old==c)*p=v;return old;}
static UINT live,allocations,fail_allocation,checks;
static void*GetProcessHeap(void){return NULL;}static void*HeapAlloc(void*h,UINT flags,size_t bytes){(void)h;assert(flags==8&&bytes);if(++allocations==fail_allocation)return NULL;void*p=calloc(1,bytes);assert(p);live++;return p;}static void HeapFree(void*h,UINT flags,void*p){(void)h;assert(!flags);if(p){assert(live);live--;free(p);}}
${methods}
${resourceMode?"struct IDirect3D"+resourcePrefix+"9{const IUnknownVtbl*lpVtbl;};":""}
${resourceMode?"typedef struct D9WG"+resourcePrefix+"Methods8 D9WG"+resourcePrefix+"Methods8;":""}
${textureTypes}
${textureMethods}
${volumeMethods}
${volumeMode?"static const D9WGVolumeMethods8*d9wg_d3d9_core_volume_methods8(struct IDirect3DVolume9*);":""}
typedef void(*D9WGCoreViewRetire8)(void*);
static const D9WGSurfaceMethods8*d9wg_d3d9_core_surface_methods8(struct IDirect3DSurface9*);
static HRESULT d9wg_d3d9_core_get_view8(IUnknown*,void**);
static HRESULT d9wg_d3d9_core_attach_view8(IUnknown*,void*,D9WGCoreViewRetire8);
${common}
${source}
${volumeSource}
${textureSource}
${resourceMode?'#define main retained_surface_main':""}
#include "tests/native/d8wg_surface_adapter_cases.h"
${resourceMode?'#undef main\n#include "tests/native/d8wg_'+resourceName+'_adapter_cases.h"':""}
`);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/adapter.c','-o',dir+'/adapter'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/adapter',[],{encoding:'utf8',timeout:30000});assert.equal(run.status,0,run.stderr+' signal='+run.signal);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
