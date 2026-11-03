"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-standalone-container-"));
try {
    const source = fs.readFileSync(process.env.D9WG_STANDALONE_CONTAINER_SOURCE || root + "/d3d9proxy/d3d9_proxy.c", "utf8"), m = /static HRESULT WINAPI surface_get_container\([^;]*?\)\s*\{/.exec(source); assert(m);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    fs.writeFileSync(dir + "/container.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int HRESULT,BOOL;typedef const int*REFIID;typedef unsigned ULONG;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define E_NOINTERFACE (-1)
#define D3DERR_INVALIDCALL (-2)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
typedef struct{ULONG refs;}Interface;typedef struct{Interface iface;}Owner;
typedef struct{Interface iface;Owner*device,*texture,*cube_texture,*swap_chain;BOOL auto_depth_stencil,texture_child;}D9Surface,IDirect3DSurface9;
static const int unknown_id=0,IID_IDirect3DDevice9=1,IID_IDirect3DResource9=2,IID_IDirect3DBaseTexture9=3,IID_IDirect3DTexture9=4,IID_IDirect3DCubeTexture9=5,surface_id=6,IID_IDirect3DSwapChain9=7,bad_id=99;
static D9Surface*surface_from_iface(IDirect3DSurface9*p){return p;}
static BOOL iid_is_unknown(REFIID p){return p&&!*p;}static BOOL guid_equal(REFIID a,REFIID b){return a&&b&&*a==*b;}
static ULONG add_ref(Interface*p){return ++p->refs;}
#define IDirect3DDevice9_AddRef add_ref
#define IDirect3DTexture9_AddRef add_ref
#define IDirect3DCubeTexture9_AddRef add_ref
#define IDirect3DSwapChain9_AddRef add_ref
${source.slice(m.index, end)}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Standalone container line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0,calls=0;Owner device={{7}},texture={{11}},cube={{13}},chain={{17}};
const int*ids[]={&unknown_id,&IID_IDirect3DDevice9,&IID_IDirect3DResource9,&IID_IDirect3DBaseTexture9,&IID_IDirect3DTexture9,&IID_IDirect3DCubeTexture9,&surface_id,&IID_IDirect3DSwapChain9,&bad_id};
for(unsigned kind=0;kind<9;kind++){
 D9Surface s={.iface={19},.device=&device};
 /* Three GPU-backed standalone targets, two CPU-only surfaces, auto depth,
  * then ordinary texture/cube levels and a chain backbuffer. */
 if(kind<3||kind==6)s.texture=&texture;if(kind==5)s.auto_depth_stencil=1;
 if(kind==6)s.texture_child=1;if(kind==7)s.cube_texture=&cube;if(kind==8)s.swap_chain=&chain;
 CHECK(surface_get_container(&s,&unknown_id,NULL)==D3DERR_INVALIDCALL);
 for(unsigned cycle=0;cycle<32;cycle++)for(unsigned i=0;i<9;i++){
  const int id=*ids[i];BOOL valid=kind<6?(id==0||id==1):kind==6?(id==0||id==2||id==3||id==4):kind==7?(id==0||id==2||id==3||id==5):(id==0||id==7);
  Interface*expected=kind<6?&device.iface:kind==6?&texture.iface:kind==7?&cube.iface:&chain.iface;ULONG before=expected->refs;void*out=(void*)(uintptr_t)1;
  CHECK(surface_get_container(&s,ids[i],&out)==(valid?D3D_OK:E_NOINTERFACE));
  CHECK(out==(valid?(void*)expected:NULL)&&expected->refs==before+(valid?1u:0u));
  if(valid)--((Interface*)out)->refs;
  CHECK(device.iface.refs==7&&texture.iface.refs==11&&cube.iface.refs==13&&chain.iface.refs==17&&s.iface.refs==19);calls++;
 }
 if(kind<6||kind==8){void*out=(void*)(uintptr_t)1;CHECK(surface_get_container(&s,NULL,&out)==E_NOINTERFACE&&!out);}
}
printf("PASS actual D3D9 standalone containers: %u checks, %u queries, five standalone storage kinds plus auto-depth/texture/cube/chain controls; exact identities, one query reference, failed outputs and no hidden-texture exposure\\n",checks,calls);return 0;}
`);
    const b = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", dir + "/container.c", "-o", dir + "/container"], {encoding: "utf8"}); assert.equal(b.status, 0, b.stderr);
    const r = spawnSync(dir + "/container", [], {encoding: "utf8", timeout: 10000}); assert.equal(r.status, 0, r.stderr + " signal=" + r.signal); process.stdout.write(r.stdout);
} finally {fs.rmSync(dir, {recursive: true, force: true});}
