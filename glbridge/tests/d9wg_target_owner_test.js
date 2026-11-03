"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-target-owner-"));
function extract(source, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    return source.slice(m.index, end);
}
try {
    const source = fs.readFileSync(process.env.D9WG_TARGET_OWNER_SOURCE || root + "/d3d9proxy/d3d9_proxy.c", "utf8");
    fs.writeFileSync(dir + "/owner.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD,ULONG;typedef int HRESULT,BOOL;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DUSAGE_RENDERTARGET 1
#define D9_MAX_RENDER_TARGETS 4
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define FAILED(v) ((v)<0)
typedef struct{const void*lpVtbl;ULONG refs;}Interface;typedef Interface IDirect3DDevice9,IDirect3DSurface9;
typedef struct Device D9Device;typedef struct{UINT handle,usage;}Texture;
typedef struct Surface{Interface iface;D9Device*device;Texture*texture,*cube_texture;UINT level,cube_face,width,height;void*shadow;BOOL auto_depth_stencil;struct{UINT handle;}*swap_chain;}D9Surface;
typedef struct{UINT X,Y,Width,Height;float MinZ,MaxZ;}D3DVIEWPORT9;typedef struct{int left,top,right,bottom;}RECT;
struct Device{Interface iface;UINT handle;D9Surface*render_target_surfaces[4];D3DVIEWPORT9 viewport;RECT scissor;};
static int g_surface_vtbl,bad_vtbl;static unsigned commands,viewport_calls,scissor_calls,checks,cases;static D9WGSetRenderTarget last;
static D9Device*device_from_iface(Interface*p){return (D9Device*)p;}static D9Surface*surface_from_iface(Interface*p){return (D9Surface*)p;}
static ULONG IDirect3DSurface9_AddRef(Interface*p){return ++p->refs;}static ULONG IDirect3DSurface9_Release(Interface*p){assert(p->refs);return --p->refs;}
static void SetRect(RECT*r,int l,int t,int right,int b){*r=(RECT){l,t,right,b};}
static HRESULT IDirect3DDevice9_SetViewport(Interface*p,const D3DVIEWPORT9*v){viewport_calls++;device_from_iface(p)->viewport=*v;return 0;}
static HRESULT IDirect3DDevice9_SetScissorRect(Interface*p,const RECT*r){scissor_calls++;device_from_iface(p)->scissor=*r;return 0;}
static BOOL emit_command(UINT op,const void*p,UINT n){assert(op==D9WG_OP_SET_RENDER_TARGET&&n==sizeof(last));last=*(const D9WGSetRenderTarget*)p;commands++;return 1;}
${["surface_target_handle", "device_set_render_target"].map(n => extract(source, n)).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Target owner line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){D9Device foreign={.handle=8};Texture texture={21,1},cube={22,1},depth={23,2};
for(unsigned cycle=0;cycle<32;cycle++)for(unsigned kind=0;kind<9;kind++)for(unsigned slot=0;slot<4;slot++){
 D9Surface old={.iface={&g_surface_vtbl,3}};D9Device d={.handle=7,.viewport={5,6,20,18,.1f,.9f},.scissor={7,8,12,14}};d.render_target_surfaces[slot]=&old;
 D9Surface s={.iface={&g_surface_vtbl,11},.device=&foreign,.texture=&texture,.level=2,.width=32,.height=24};
 if(kind==1){s.texture=NULL;s.cube_texture=&cube;s.cube_face=5;}
 if(kind==2){s.texture=NULL;s.level=0;}
 if(kind==3){s.texture=&depth;}
 if(kind==4){s.texture=NULL;s.auto_depth_stencil=1;}
 if(kind==5)s.shadow=&old;
 if(kind==6){s.iface.lpVtbl=&bad_vtbl;s.device=&d;}
 if(kind==7){s.texture=NULL;s.device=NULL;}
 if(kind==8){s.iface.lpVtbl=&bad_vtbl;s.device=&foreign;}
 D9Device before=d;D9Surface before_s=s;unsigned c=commands,v=viewport_calls,r=scissor_calls;
 CHECK(device_set_render_target(&d.iface,slot,&s.iface)==D3DERR_INVALIDCALL);
 CHECK(!memcmp(&d,&before,sizeof(d))&&!memcmp(&s,&before_s,sizeof(s))&&old.iface.refs==3);
 CHECK(commands==c&&viewport_calls==v&&scissor_calls==r);cases++;
}
/* The foreign-vtable guard must not read beyond a bare interface. */
{D9Device d={.handle=7};Interface bare={&bad_vtbl,11};CHECK(device_set_render_target(&d.iface,0,&bare)==D3DERR_INVALIDCALL&&bare.refs==11);cases++;}
for(unsigned kind=0;kind<4;kind++)for(unsigned slot=0;slot<4;slot++){
 D9Surface old={.iface={&g_surface_vtbl,3}};D9Device d={.handle=7,.viewport={5,6,20,18,.1f,.9f},.scissor={7,8,12,14}};d.render_target_surfaces[slot]=&old;
 D9Surface s={.iface={&g_surface_vtbl,11},.device=&d,.texture=&texture,.level=2,.width=32,.height=24};
 if(kind==1){s.texture=NULL;s.cube_texture=&cube;s.cube_face=5;}
 if(kind==2){s.texture=NULL;s.level=0;}
 D3DVIEWPORT9 old_v=d.viewport;RECT old_r=d.scissor;unsigned c=commands,v=viewport_calls,r=scissor_calls;
 CHECK(!device_set_render_target(&d.iface,slot,&s.iface)&&d.render_target_surfaces[slot]==&s&&s.iface.refs==12&&old.iface.refs==2);
 CHECK(commands==c+1&&last.device_handle==7&&last.target_index==slot&&last.color_texture_handle==(kind==1?22u:kind==2?0u:21u)&&last.color_level==(kind==2?0u:2u)&&last.color_face==(kind==1?5u:0u));
 CHECK(viewport_calls==v+!slot&&scissor_calls==r+!slot);
 CHECK(slot?(!memcmp(&d.viewport,&old_v,sizeof(old_v))&&!memcmp(&d.scissor,&old_r,sizeof(old_r))):(d.viewport.X==0&&d.viewport.Y==0&&d.viewport.Width==32&&d.viewport.Height==24&&d.viewport.MinZ==0&&d.viewport.MaxZ==1&&d.scissor.left==0&&d.scissor.top==0&&d.scissor.right==32&&d.scissor.bottom==24));
 CHECK(!device_set_render_target(&d.iface,slot,&s.iface)&&s.iface.refs==12&&old.iface.refs==2);cases++;
}
for(unsigned slot=0;slot<5;slot++){D9Device d={.handle=7};unsigned c=commands;CHECK(device_set_render_target(&d.iface,slot,NULL)==(slot&&slot<4?0:D3DERR_INVALIDCALL));CHECK(commands==c+(slot&&slot<4));cases++;}
printf("PASS actual D3D9 target owner: %u checks, %u cases; nine foreign/invalid shapes, all four slots, exact rejection atomicity, bare-interface safety, local texture/cube/backbuffer, same-target references and viewport/scissor/NULL controls\\n",checks,cases);return 0;}
`);
    const b = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-I", root, dir + "/owner.c", "-o", dir + "/owner"], {encoding: "utf8"}); assert.equal(b.status, 0, b.stderr);
    const r = spawnSync(dir + "/owner", [], {encoding: "utf8", timeout: 10000}); assert.equal(r.status, 0, r.stderr + " signal=" + r.signal); process.stdout.write(r.stdout);
} finally {fs.rmSync(dir, {recursive: true, force: true});}
