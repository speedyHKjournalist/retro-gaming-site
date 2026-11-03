"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = process.env.D8WG_CONTAINER_ROOT || path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-additional-container-"));
function extract(s, name) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name); let end = s.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;} return s.slice(m.index, end);}
try {
    const source = fs.readFileSync(process.env.D8WG_CONTAINER_SOURCE || root + "/d3d8proxy/d3d8_proxy.c", "utf8");
    fs.writeFileSync(dir + "/container.c", `#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
typedef int HRESULT,BOOL;typedef const unsigned*REFIID;
#define WINAPI
#define E_POINTER (-1)
#define E_NOINTERFACE (-2)
#define S_OK 0
#define D8WG_TRACE_ERROR(v) (v)
static const unsigned iid_unknown=1,IID_IDirect3DResource8=2,IID_IDirect3DBaseTexture8=3,IID_IDirect3DTexture8=4,IID_IDirect3DCubeTexture8=5,IID_IDirect3DDevice8=6;
static BOOL iid_is_unknown(REFIID iid){return *iid==iid_unknown;}
static BOOL guid_equal(REFIID a,REFIID b){return *a==*b;}
typedef struct{unsigned refs;}Interface;
typedef struct{Interface iface;}Device,Texture,Cube;
typedef struct{Interface iface;Device*device;Texture*texture;Cube*cube;BOOL additional_back_buffer,device_container;}D8Surface;
typedef Interface IDirect3DSurface8;
static D8Surface*surface_from_iface(Interface*p){return (D8Surface*)p;}
static unsigned add_ref(Interface*p){return ++p->refs;}
#define IDirect3DTexture8_AddRef add_ref
#define IDirect3DCubeTexture8_AddRef add_ref
#define IDirect3DDevice8_AddRef add_ref
${extract(source, "surface_get_container")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D8 additional container line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0;Device device={{10}};Texture texture={{20}};Cube cube={{30}};D8Surface surface={.device=&device,.texture=&texture,.additional_back_buffer=1};unsigned iids[]={1,2,3,4,5,6,7,8,0xdeadbeef};
for(unsigned repeat=0;repeat<3;repeat++)for(unsigned i=0;i<sizeof(iids)/sizeof(iids[0]);i++){
 void*out=(void*)(uintptr_t)0xdeadbeef;unsigned before=device.iface.refs;HRESULT hr=surface_get_container(&surface.iface,&iids[i],&out);BOOL accepted=iids[i]==1||iids[i]==6;
 CHECK(hr==(accepted?S_OK:E_NOINTERFACE));CHECK(out==(accepted?(void*)&device.iface:NULL));CHECK(device.iface.refs==before+accepted&&texture.iface.refs==20&&cube.iface.refs==30);if(accepted)device.iface.refs--;
}
void*out=(void*)(uintptr_t)0xdeadbeef;CHECK(surface_get_container(&surface.iface,NULL,&out)==E_NOINTERFACE&&!out);CHECK(surface_get_container(&surface.iface,&IID_IDirect3DDevice8,NULL)==E_POINTER);
/* The early-chain-release state still has the same retained device/texture. */
out=NULL;CHECK(!surface_get_container(&surface.iface,&IID_IDirect3DDevice8,&out)&&out==&device.iface&&device.iface.refs==11);device.iface.refs--;
/* Ordinary 2D/cube views keep their texture container, including resource/base IIDs. */
surface.additional_back_buffer=0;
for(unsigned type=0;type<2;type++){
 surface.texture=type?NULL:&texture;surface.cube=type?&cube:NULL;Interface*expected=type?&cube.iface:&texture.iface;
 for(unsigned i=1;i<=6;i++){out=(void*)(uintptr_t)0xdeadbeef;unsigned before=expected->refs;BOOL accepted=i<=3||i==(type?5:4);CHECK(surface_get_container(&surface.iface,&i,&out)==(accepted?S_OK:E_NOINTERFACE)&&out==(accepted?(void*)expected:NULL));CHECK(expected->refs==before+accepted&&device.iface.refs==10);if(accepted)expected->refs--;}
}
surface.texture=NULL;surface.cube=NULL;
for(unsigned i=1;i<=6;i++){out=(void*)(uintptr_t)0xdeadbeef;BOOL accepted=i==1||i==6;CHECK(surface_get_container(&surface.iface,&i,&out)==(accepted?S_OK:E_NOINTERFACE)&&out==(accepted?(void*)&device.iface:NULL));CHECK(device.iface.refs==10u+(unsigned)accepted);if(accepted)device.iface.refs--;}
CHECK(device.iface.refs==10&&texture.iface.refs==20&&cube.iface.refs==30);
printf("PASS actual D3D8 additional container: %u checks; device/IUnknown identity, precise AddRef, rejected texture IIDs/output, retained-device query and ordinary 2D/cube/implicit containers\\n",checks);return 0;}
`);
    const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", dir + "/container.c", "-o", dir + "/container"], {encoding: "utf8"}); assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(dir + "/container", [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal); process.stdout.write(run.stdout);
} finally {fs.rmSync(dir, {recursive: true, force: true});}
