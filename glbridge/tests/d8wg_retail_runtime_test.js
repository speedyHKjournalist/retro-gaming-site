"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), eight = fs.readFileSync(root + "/d3d8proxy/d3d8_core_device.c", "utf8"), nine = fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8");
function extract(source, name) {
    const match = new RegExp("^[A-Za-z_][^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{", "m").exec(source);
    assert(match, name);let end = source.indexOf("{", match.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++;if (source[end] === "}") depth--;}
    return source.slice(match.index, end);
}
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-retail-"));
try {
    fs.writeFileSync(dir + "/retail.c", `#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UINT,DWORD;typedef int32_t INT,HRESULT;typedef unsigned D3DPRIMITIVETYPE,D3DFORMAT;
#define WINAPI
#define TRUE 1
#define D3D_OK 0
#define S_FALSE 1
#define E_FAIL (-1)
#define D3DERR_INVALIDCALL (-2)
#define SUCCEEDED(hr) ((hr)>=0)
#define CopyMemory memcpy
typedef struct{unsigned api;struct{void*buffer;}streams[1];}IDirect3DDevice9;
typedef struct{HRESULT(*draw_indexed_primitive_up)(IDirect3DDevice9*,UINT,UINT,UINT,UINT,const void*,UINT,const void*,UINT);}Methods;
typedef struct{IDirect3DDevice9*backend;const Methods*methods;struct{UINT base;}live;void*recording;}D8CoreDevice,IDirect3DDevice8;
static D8CoreDevice*core_device8(IDirect3DDevice8*p){return p;}
static IDirect3DDevice9*device_from_iface(IDirect3DDevice9*p){return p;}
static unsigned device_compatibility_api(IDirect3DDevice9*p){return p->api;}
static unsigned calls;static HRESULT result;static INT seen_base;
static HRESULT draw_up(IDirect3DDevice9*d,UINT type,UINT min,UINT vertices,UINT primitives,const void*indices,UINT format,const void*data,UINT stride){(void)d;(void)type;(void)min;(void)vertices;(void)primitives;(void)indices;(void)format;(void)data;(void)stride;calls++;return result;}
static HRESULT IDirect3DDevice9_DrawIndexedPrimitive(IDirect3DDevice9*d,UINT type,INT base,UINT min,UINT count,UINT start,UINT primitives){(void)d;(void)type;(void)min;(void)count;(void)start;(void)primitives;calls++;seen_base=base;return result;}
${extract(eight, "core_device8_GetInfo")}
${extract(eight, "core_device8_DrawIndexedPrimitiveUP")}
${extract(nine, "d9wg_d3d9_core_draw_indexed8")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Retail API line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0;IDirect3DDevice9 backend={.api=8};Methods methods={draw_up};D8CoreDevice device={.backend=&backend,.methods=&methods};unsigned char info[32],before[32];memset(info,0xa5,32);memcpy(before,info,32);
for(unsigned id=0;id<256;id++){CHECK(core_device8_GetInfo(&device,id,info,32)==(id<4?E_FAIL:S_FALSE));CHECK(!memcmp(info,before,32));}
for(unsigned recording=0;recording<2;recording++)for(unsigned fail=0;fail<2;fail++)for(unsigned primitives=0;primitives<2;primitives++){
 unsigned recorded_base=123;device.recording=recording?&recorded_base:NULL;device.live.base=99;result=fail?E_FAIL:D3D_OK;
 CHECK(core_device8_DrawIndexedPrimitiveUP(&device,4,0,3,primitives,info,101,info,16)==result);
 CHECK(device.live.base==(fail||!primitives?99:0)&&recorded_base==123);
}
const UINT bases[]={0,1,0x7fffffffu,0x80000000u,0xffffffffu};
for(unsigned stream=0;stream<2;stream++)for(unsigned b=0;b<5;b++)for(unsigned fail=0;fail<2;fail++){
 backend.streams[0].buffer=stream?&device:NULL;unsigned old=calls;result=fail?E_FAIL:D3D_OK;
 CHECK(d9wg_d3d9_core_draw_indexed8(&backend,4,bases[b],0,3,0,1)==(stream?result:D3D_OK));
 CHECK(calls==old+stream);if(stream)CHECK((UINT)seen_base==bases[b]);
}
printf("PASS production retail adapters: %u checks; GetInfo immutable output, live-only UP base cleanup, failure preservation, missing-stream no-op and signed base bits\\n",checks);return 0;}
`);
    const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", dir + "/retail.c", "-o", dir + "/retail"], {encoding:"utf8"});
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(dir + "/retail", [], {encoding:"utf8"});assert.equal(run.status, 0, run.stderr);process.stdout.write(run.stdout);
} finally {fs.rmSync(dir, {recursive:true, force:true});}
