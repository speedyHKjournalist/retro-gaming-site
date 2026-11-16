"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-backbuffer-selector-"));
function extract(s, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    return s.slice(m.index, end);
}
try {
    for (const api of [8, 9]) {
        const source = readComparisonSource(process.env["D" + api + "WG_SELECTOR_SOURCE"] || root + "/d3d" + api + "proxy/d3d" + api + "_proxy.c", "utf8");
        fs.writeFileSync(dir + "/selector.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint32_t UINT,DWORD,ULONG;typedef int32_t LONG,HRESULT;typedef int D3DBACKBUFFER_TYPE,BOOL;
#define WINAPI
#define TRUE 1
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-2)
#define D3DBACKBUFFER_TYPE_MONO 0
#define SUCCEEDED(v) ((v)>=0)
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
typedef struct {unsigned unused;}Store;typedef struct {LONG refs;Store store;}Owner;
typedef struct Surface{unsigned refs;Store*private_data_owner;Owner*retained_private_data;}Surface;
typedef struct {int iface;Surface implicit;struct{UINT BackBufferCount;}present;}Device;
typedef struct {Device*device;UINT handle;Surface*back_buffer;struct{UINT BackBufferCount;}present;Owner*private_data_owner;}Chain;
typedef Chain D${api}SwapChain,IDirect3DSwapChain${api};typedef Surface D${api}Surface,IDirect3DSurface${api};
static unsigned calls,allocations,live,fail_alloc;static D3DBACKBUFFER_TYPE forwarded_type;
${api===8?`static void IDirect3DSurface8_AddRef(Surface*s){assert(s);s->refs++;}`:`static Chain*swap_chain_from_iface(Chain*c){return c;}
static void IDirect3DSurface9_AddRef(Surface*s){assert(s);s->refs++;}
static HRESULT device_get_back_buffer(int*iface,UINT chain,UINT index,D3DBACKBUFFER_TYPE type,Surface**out){Device*d=(Device*)iface;assert(!chain&&out);calls++;forwarded_type=type;*out=NULL;if(index)return D3DERR_INVALIDCALL;*out=&d->implicit;IDirect3DSurface9_AddRef(*out);return D3D_OK;}`}
${api===9?'static HRESULT swap_chain_cached_buffer(Chain*c,UINT i,Surface**out){(void)c;(void)i;(void)out;assert(0);return D3DERR_INVALIDCALL;}':''}
${extract(source, api===8?"swapchain_get_backbuffer":"swap_chain_get_back_buffer")}
#define GET(c,i,t,o) ${api===8?"swapchain_get_backbuffer(c,i,t,o)":"swap_chain_get_back_buffer(c,i,t,o)"}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Backbuffer selector API ${api} line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0;Device d={.present={1}};Owner owner={.refs=1};Surface backing={.refs=1};Chain c={.device=&d,.handle=77,.back_buffer=&backing,.present={1},.private_data_owner=&owner};
const uint32_t types[]={0,1,2,0xdeadbeef,0xffffffff};const UINT indices[]={1,2,3,0xffffffff};
for(unsigned route=0;route<${api===8?1:2};route++){
 c.handle=route?0:77;
 for(unsigned t=0;t<5;t++){
  unsigned before=calls;CHECK(GET(&c,0,(int32_t)types[t],NULL)==D3DERR_INVALIDCALL&&calls==before&&owner.refs==1&&backing.refs==1&&!d.implicit.refs);
  for(unsigned i=0;i<4;i++){Surface*out=(void*)(uintptr_t)0x1234;CHECK(GET(&c,indices[i],(int32_t)types[t],&out)==D3DERR_INVALIDCALL&&out==(void*)(uintptr_t)0x1234&&calls==before&&owner.refs==1&&backing.refs==1&&!d.implicit.refs);}
  Surface*out=NULL;CHECK(GET(&c,0,(int32_t)types[t],&out)==D3D_OK&&out);
  ${api===8?`CHECK(out==&backing&&out->refs==2&&owner.refs==1&&calls==before&&!live&&!allocations);out->refs--;`:`CHECK(out==(route?&d.implicit:&backing)&&out->refs==(route?1u:2u)&&owner.refs==1&&calls==before+route);if(route)CHECK(forwarded_type==(int32_t)types[t]);out->refs--;`}
 }
}
${api===8?`fail_alloc=1;Surface*out=(void*)(uintptr_t)0x1234;CHECK(GET(&c,0,2,&out)==D3D_OK&&out==&backing&&out->refs==2&&owner.refs==1&&!live&&!allocations);out->refs--;`:'(void)allocations;(void)live;(void)fail_alloc;'}
printf("PASS actual API ${api} backbuffer selectors: %u checks; ignored stereo values, NULL/bad-index boundaries, preserved outputs, exact successful references and unchanged failure ownership\\n",checks);return 0;}
`);
        const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-I", root, dir + "/selector.c", "-o", dir + "/selector"], {encoding: "utf8"}); assert.equal(build.status, 0, build.stderr);
        const run = spawnSync(dir + "/selector", [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal); process.stdout.write(run.stdout);
    }
} finally {fs.rmSync(dir, {recursive: true, force: true});}
