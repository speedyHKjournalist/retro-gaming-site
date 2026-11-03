"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-display-mode-"));
function extract(source, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name);
    let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) { if (source[end] === "{") depth++; if (source[end] === "}") depth--; }
    return source.slice(m.index, end);
}
try {
    for (const api of [8, 9]) {
        const source = fs.readFileSync(process.env["D" + api + "WG_DISPLAY_FRONTEND"] || root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        fs.writeFileSync(dir + "/mode.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UINT,DWORD;typedef int32_t HRESULT;typedef int BOOL,D3DFORMAT;
typedef struct{UINT dmSize,dmPelsWidth,dmPelsHeight,dmBitsPerPel,dmDisplayFrequency;UINT reserved[4];}DEVMODEA;
typedef struct{UINT Width,Height,RefreshRate;D3DFORMAT Format;}D3DDISPLAYMODE;
typedef struct{int value;}Interface;typedef struct{Interface iface;}Factory;
typedef struct{Factory*parent;struct{UINT AdapterOrdinal;}creation;D3DDISPLAYMODE display_mode;}Device;
typedef struct{Device*device;UINT handle;}Chain;
typedef Factory D${api}Direct3D;typedef Interface IDirect3D${api};typedef Device D${api}Device,IDirect3DDevice${api};typedef Chain D${api}SwapChain,IDirect3DSwapChain${api};
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DFMT_X8R8G8B8 22
#define D3DFMT_R5G6B5 23
#define ENUM_CURRENT_SETTINGS UINT32_MAX
#define SUCCEEDED(v) ((v)>=0)
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...) ((void)0)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define ZeroMemory(p,n) memset(p,0,n)
static DEVMODEA physical;static unsigned fail_query,queries;
static BOOL EnumDisplaySettingsA(const char*name,UINT index,DEVMODEA*out){assert(!name&&index==UINT32_MAX&&out->dmSize==sizeof(*out));for(unsigned i=0;i<4;i++)assert(!out->reserved[i]);queries++;*out=physical;return !fail_query;}
#include "${process.env.D9WG_DISPLAY_HEADER || root + "/runtime/d9wg_display_mode.h"}"
static Device*device_from_iface(Device*d){return d;}
${api === 9 ? "static Chain*swap_chain_from_iface(Chain*c){return c;}" : ""}
${["fill_display_mode", "d3d_get_adapter_display_mode", "device_get_display_mode", ...(api === 9 ? ["swap_chain_get_display_mode"] : [])].map(n => extract(source, n)).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Display mode API ${api} line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
static HRESULT query(unsigned method,Factory*f,Device*d,Chain*c,D3DDISPLAYMODE*out){(void)c;return method==0?d3d_get_adapter_display_mode(&f->iface,0,out):method==1?device_get_display_mode(d,${api === 9 ? "0," : ""}out):${api === 9 ? "swap_chain_get_display_mode(c,out)" : "D3DERR_INVALIDCALL"};}
int main(void){unsigned checks=0;Factory parent={0};Device device={.parent=&parent,.display_mode={37,29,123,99}},before=device;Chain chain={&device,123},implicit={&device,0};
struct Profile{UINT w,h,bits,hz,expected_hz,format,fail;}profiles[]={
 {800,600,32,60,60,22,0},{640,480,16,75,75,23,0},{1024,768,32,120,120,22,0},{1280,960,24,1,60,22,0},{8192,8192,32,0,60,22,0},{1920,1080,32,59,59,22,0},
 {1920,1080,32,144,60,22,1},{0,600,32,85,60,22,0},{800,0,16,85,60,22,0}};
for(unsigned p=0;p<sizeof(profiles)/sizeof(profiles[0]);p++){
 struct Profile v=profiles[p];physical=(DEVMODEA){.dmPelsWidth=v.w,.dmPelsHeight=v.h,.dmBitsPerPel=v.bits,.dmDisplayFrequency=v.hz};fail_query=v.fail;
 UINT w=v.fail||!v.w||!v.h?1024:v.w,h=v.fail||!v.w||!v.h?768:v.h,format=v.fail||!v.w||!v.h?22:v.format;
 unsigned format_queries=queries;CHECK(d9wg_display_mode_backbuffer_format(0)==format&&queries==format_queries+1);
 for(UINT explicit_format=21;explicit_format<=23;explicit_format++)CHECK(d9wg_display_mode_backbuffer_format(explicit_format)==explicit_format&&queries==format_queries+1);
 for(unsigned method=0;method<${api === 9 ? 3 : 2};method++)for(unsigned c=0;c<2;c++){
  struct{uint32_t left;D3DDISPLAYMODE mode;uint32_t right;}out;memset(&out,0xcd,sizeof(out));unsigned old_queries=queries;
  CHECK(query(method,&parent,&device,c?&chain:&implicit,&out.mode)==D3D_OK);CHECK(out.mode.Width==w&&out.mode.Height==h&&out.mode.Format==(int)format&&out.mode.RefreshRate==v.expected_hz);
  CHECK(queries==old_queries+1&&out.left==0xcdcdcdcdu&&out.right==0xcdcdcdcdu&&!memcmp(&device,&before,sizeof(device))&&chain.handle==123&&implicit.handle==0);
 }
}
fail_query=0;D3DDISPLAYMODE out,before_out;memset(&out,0xa5,sizeof(out));before_out=out;unsigned old_queries=queries;
CHECK(d3d_get_adapter_display_mode(&parent.iface,1,&out)==-1&&queries==old_queries&&!memcmp(&out,&before_out,sizeof(out)));
CHECK(d3d_get_adapter_display_mode(&parent.iface,UINT32_MAX,&out)==-1&&queries==old_queries&&!memcmp(&out,&before_out,sizeof(out)));
for(unsigned method=0;method<${api === 9 ? 3 : 2};method++)CHECK(query(method,&parent,&device,&chain,NULL)==-1&&queries==old_queries);
${api === 9 ? "CHECK(device_get_display_mode(&device,1,&out)==-1&&queries==old_queries&&!memcmp(&out,&before_out,sizeof(out)));" : ""}
device.creation.AdapterOrdinal=1;CHECK(query(1,&parent,&device,&chain,&out)==-1&&queries==old_queries&&!memcmp(&out,&before_out,sizeof(out)));
${api === 9 ? "CHECK(query(2,&parent,&device,&chain,&out)==-1&&queries==old_queries&&!memcmp(&out,&before_out,sizeof(out)));" : ""}
printf("PASS actual API ${api} display queries: %u checks; live adapter mode on factory/device/chains, independent buffer geometry, 16/24/32-bit and refresh/fallback profiles, invalid inputs, guards and no cached mutation\\n",checks);return 0;}
`);
        const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", dir + "/mode.c", "-o", dir + "/mode"], {encoding: "utf8"});
        assert.equal(build.status, 0, build.stderr);
        const run = spawnSync(dir + "/mode", [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal); process.stdout.write(run.stdout);
    }
} finally { fs.rmSync(dir, {recursive: true, force: true}); }
