"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-standalone-container-"));
function extract(source, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    return source.slice(m.index, end);
}
try {
    const source = fs.readFileSync(process.env.D8WG_STANDALONE_CONTAINER_SOURCE || root + "/d3d8proxy/d3d8_proxy.c", "utf8");
    fs.writeFileSync(dir + "/container.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int HRESULT,BOOL,WINBOOL,D3DFORMAT,D3DMULTISAMPLE_TYPE;typedef const int*REFIID;typedef unsigned ULONG,UINT;
#define WINAPI
#define TRUE 1
#define S_OK 0
#define E_POINTER (-1)
#define E_NOINTERFACE (-2)
#define D3DERR_INVALIDCALL (-3)
#define E_OUTOFMEMORY (-4)
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_RENDERTARGET 1
#define D3DMULTISAMPLE_NONE 0
#define FAILED(v) ((v)<0)
#define SUCCEEDED(v) ((v)>=0)
#define D8WG_TRACE_ERROR(v) (v)
typedef struct{ULONG refs;}Interface;typedef struct{Interface iface;}D8Device,IDirect3DDevice8,Owner;
typedef struct{Interface iface;BOOL lockable_render_target;}D8Texture;typedef Interface IDirect3DTexture8;
typedef struct{Interface iface;D8Device*device;D8Texture*texture;Owner*cube;BOOL additional_back_buffer,device_container;}D8Surface,IDirect3DSurface8;
static const int unknown_id=0,IID_IDirect3DDevice8=1,IID_IDirect3DResource8=2,IID_IDirect3DBaseTexture8=3,IID_IDirect3DTexture8=4,IID_IDirect3DCubeTexture8=5,surface_id=6,chain_id=7,bad_id=99;
static D8Surface*surface_from_iface(IDirect3DSurface8*p){return p;}
static D8Texture*texture_from_iface(IDirect3DTexture8*p){return (D8Texture*)p;}
static D8Device*device_from_iface(IDirect3DDevice8*p){return p;}
static BOOL iid_is_unknown(REFIID p){return p&&!*p;}static BOOL guid_equal(REFIID a,REFIID b){return a&&b&&*a==*b;}
static ULONG add_ref(Interface*p){return ++p->refs;}
#define IDirect3DDevice8_AddRef add_ref
#define IDirect3DTexture8_AddRef add_ref
#define IDirect3DCubeTexture8_AddRef add_ref
static D8Texture storage;static D8Surface view;static BOOL fail_create,fail_view;
static BOOL supported_multisample_type(UINT n){return n==0||n==4;}
static BOOL supported_depth_stencil_format(D3DFORMAT f){return f==75;}
static HRESULT create_texture_internal(D8Device*d,UINT w,UINT h,UINT levels,UINT usage,D3DFORMAT f,UINT pool,UINT ms,IDirect3DTexture8**out){assert(d&&w==32&&h==24&&levels==1&&usage==1&&f==22&&pool==0&&(ms==0||ms==4));if(fail_create)return E_OUTOFMEMORY;storage=(D8Texture){.iface={1}};view=(D8Surface){.device=d,.texture=&storage};*out=&storage.iface;return 0;}
static HRESULT create_depth_stencil_texture(D8Device*d,UINT w,UINT h,D3DFORMAT f,UINT ms,D8Texture**out){assert(f==75);IDirect3DTexture8*p=NULL;HRESULT hr=create_texture_internal(d,w,h,1,1,22,0,ms,&p);if(SUCCEEDED(hr))*out=(D8Texture*)p;return hr;}
static HRESULT IDirect3DTexture8_GetSurfaceLevel(Interface*p,UINT level,D8Surface**out){D8Texture*t=(D8Texture*)p;assert(t==&storage&&!level&&t->iface.refs==1);if(fail_view)return E_OUTOFMEMORY;t->iface.refs++;*out=&view;return 0;}
static ULONG IDirect3DTexture8_Release(Interface*p){D8Texture*t=(D8Texture*)p;assert(t==&storage&&t->iface.refs);return --t->iface.refs;}
${["surface_get_container", "device_create_render_target", "device_create_depth_surface"].map(n => extract(source, n)).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D8 standalone container line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0,calls=0;D8Device device={{7}};D8Texture texture={.iface={11}};Owner cube={{13}};
const int*ids[]={&unknown_id,&IID_IDirect3DDevice8,&IID_IDirect3DResource8,&IID_IDirect3DBaseTexture8,&IID_IDirect3DTexture8,&IID_IDirect3DCubeTexture8,&surface_id,&chain_id,&bad_id};
for(unsigned kind=0;kind<10;kind++){
 D8Surface ordinary={.iface={19},.device=&device},*s=&ordinary;
 if(kind<4){D8Surface*out=NULL;HRESULT hr=kind<2?device_create_render_target(&device,32,24,22,kind%2?4:0,0,&out):device_create_depth_surface(&device,32,24,75,kind%2?4:0,&out);CHECK(hr==0&&out==&view&&out->device_container&&storage.iface.refs==1);s=out;}
 if(kind==6||kind==7||kind==9)s->texture=&texture;
 if(kind==8)s->cube=&cube;if(kind==9)s->additional_back_buffer=1;
 CHECK(surface_get_container(s,&unknown_id,NULL)==E_POINTER);
 for(unsigned cycle=0;cycle<32;cycle++)for(unsigned i=0;i<9;i++){
  const int id=*ids[i];BOOL valid=(kind<6||kind==9)?(id==0||id==1):kind<8?(id==0||id==2||id==3||id==4):(id==0||id==2||id==3||id==5);
  Interface*expected=(kind<6||kind==9)?&device.iface:kind<8?&texture.iface:&cube.iface;ULONG before=expected->refs;void*out=(void*)(uintptr_t)1;
  CHECK(surface_get_container(s,ids[i],&out)==(valid?S_OK:E_NOINTERFACE));
  CHECK(out==(valid?(void*)expected:NULL)&&expected->refs==before+(valid?1u:0u));
  if(valid)--((Interface*)out)->refs;
  CHECK(device.iface.refs==7&&texture.iface.refs==11&&cube.iface.refs==13&&s->iface.refs==(kind<4?0u:19u));calls++;
 }
 void*out=(void*)(uintptr_t)1;CHECK(surface_get_container(s,NULL,&out)==E_NOINTERFACE&&!out);
 if(kind<4)CHECK(IDirect3DTexture8_Release(&storage.iface)==0);
}
for(unsigned depth=0;depth<2;depth++)for(unsigned failure=0;failure<2;failure++){
 fail_create=!failure;fail_view=failure;view.device_container=0;D8Surface*out=(void*)(uintptr_t)1;
 HRESULT hr=depth?device_create_depth_surface(&device,32,24,75,0,&out):device_create_render_target(&device,32,24,22,0,0,&out);
 CHECK(hr==E_OUTOFMEMORY&&!out&&!view.device_container&&!storage.iface.refs);fail_create=fail_view=0;
}
CHECK(device_create_render_target(&device,32,24,22,0,0,NULL)==D3DERR_INVALIDCALL);
CHECK(device_create_depth_surface(&device,32,24,75,0,NULL)==D3DERR_INVALIDCALL);
printf("PASS actual D3D8 standalone containers: %u checks, %u queries; actual RT/depth constructors, CPU/implicit/additional controls, ordinary texture/cube identity, exact query references and failed creation cleanup\\n",checks,calls);return 0;}
`);
    const b = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", dir + "/container.c", "-o", dir + "/container"], {encoding: "utf8"}); assert.equal(b.status, 0, b.stderr);
    const r = spawnSync(dir + "/container", [], {encoding: "utf8", timeout: 10000}); assert.equal(r.status, 0, r.stderr + " signal=" + r.signal); process.stdout.write(r.stdout);
} finally {fs.rmSync(dir, {recursive: true, force: true});}
