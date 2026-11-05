"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8wg-chain-transport-"));
function extract(s, name) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    return s.slice(m.index, end);
}
try {
    const source = readComparisonSource(process.env.D8WG_CHAIN_TRANSPORT_SOURCE || root + "/d3d8proxy/d3d8_proxy.c", "utf8");
    fs.writeFileSync(dir + "/transport.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef int BOOL;typedef uint32_t UINT;typedef void*HWND;
#define TRUE 1
#define CopyMemory(p,s,n) memcpy(p,s,n)
typedef struct{UINT handle;}D8Device;
typedef struct{D8Device*device;UINT handle;struct{HWND hDeviceWindow;}present;}D8SwapChain;
typedef struct{UINT device_handle,hwnd;int32_t x,y;UINT width,height;}D8ClientArea;
static int g_transport_lock;static unsigned locked,retries,captures,reserves,submits,fail_reserve,fail_submit;static UINT width,height;static HWND selected;static D9WGPresentSwapChain command;
static void d9wg_window_watch_retry(void){assert(!locked);retries++;}
static void EnterCriticalSection(int*p){assert(p==&g_transport_lock&&!locked);locked=1;}
static void LeaveCriticalSection(int*p){assert(p==&g_transport_lock&&locked);locked=0;}
static void capture_surface(D8Device*d,HWND w,D8ClientArea*out){assert(locked&&d->handle==77&&w==selected);captures++;*out=(D8ClientArea){.device_handle=77,.hwnd=(UINT)(uintptr_t)w,.x=-30,.y=40,.width=width,.height=height};}
static BOOL reserve_command_locked(UINT op,UINT n,UINT blob,void*a,uint8_t**out,void*b){assert(locked&&op==D9WG_OP_PRESENT_SWAP_CHAIN&&n==sizeof(command)&&!blob&&!a&&!b);reserves++;if(fail_reserve)return 0;*out=(uint8_t*)&command;return 1;}
static BOOL submit_batch_locked(BOOL present){assert(locked&&present);submits++;return !fail_submit;}
${extract(source, "emit_present_swap_chain_and_flush")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D8 chain transport line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){unsigned checks=0;D8Device d={77};D8SwapChain c={.device=&d,.handle=100,.present={.hDeviceWindow=(void*)(uintptr_t)0x55}};
for(unsigned mode=0;mode<6;mode++){
 selected=mode==1?(void*)(uintptr_t)0x66:c.present.hDeviceWindow;fail_reserve=mode==2;fail_submit=mode==3;width=mode==4?0:80;height=mode==4?0:72;if(mode==5)c.handle=101;
 memset(&command,0xab,sizeof(command));D9WGPresentSwapChain before=command;unsigned old_retries=retries,old_captures=captures,old_reserves=reserves,old_submits=submits;
 CHECK(emit_present_swap_chain_and_flush(&c,mode==1?selected:NULL)==(!fail_reserve&&!fail_submit));
 CHECK(!locked&&retries==old_retries+1&&captures==old_captures+1&&reserves==old_reserves+1&&submits==old_submits+!fail_reserve);
 if(fail_reserve){CHECK(!memcmp(&command,&before,sizeof(command)));continue;}
 CHECK(command.device_handle==77&&command.swap_chain_handle==c.handle&&command.hwnd==(UINT)(uintptr_t)selected);
 CHECK(command.x==-30&&command.y==40&&command.width==width&&command.height==height&&!command.reserved);
}
printf("PASS actual D3D8 chain transport: %u checks; explicit chain opcode/handle, selected client geometry, override/empty windows, lock lifetime and reserve/submit failure propagation\\n",checks);return 0;}
`);
    const build = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-I", root, dir + "/transport.c", "-o", dir + "/transport"], {encoding: "utf8"}); assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(dir + "/transport", [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stdout + run.stderr + " signal=" + run.signal); process.stdout.write(run.stdout);
} finally {fs.rmSync(dir, {recursive: true, force: true});}
