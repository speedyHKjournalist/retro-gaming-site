"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=readComparisonSource(process.env.D8WG_INDEX_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-indices-"));
function extract(name){const m=new RegExp("static HRESULT WINAPI "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
try{const code=`#include <stdint.h>
#include <stdio.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT;typedef int BOOL,HRESULT;
#define WINAPI
#define TRUE 1
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D8WG_TRACE_ERROR(x) (x)
typedef struct{const void *lpVtbl;}IDirect3DIndexBuffer8;
typedef struct D8Device D8Device;typedef D8Device IDirect3DDevice8;
typedef struct D8IndexBuffer{IDirect3DIndexBuffer8 iface;D8Device *device;UINT handle;unsigned refs;}D8IndexBuffer;
typedef struct D8StateSnapshot{D8IndexBuffer *index_buffer;UINT base_vertex_index;BOOL indices_mask;}D8StateSnapshot;
typedef struct{D8StateSnapshot state;}StateBlock;
struct D8Device{UINT handle,base_vertex_index;D8IndexBuffer *index_buffer;StateBlock *recording_state_block;};
static const int g_ib_vtbl=3;static unsigned emits;static UINT last_handle;
static D8Device *device_from_iface(D8Device*p){return p;}static D8IndexBuffer *ib_from_iface(IDirect3DIndexBuffer8*p){return(void*)p;}
#define IDirect3DIndexBuffer8_AddRef(p) (++ib_from_iface(p)->refs)
#define IDirect3DIndexBuffer8_Release(p) (--ib_from_iface(p)->refs)
static BOOL emit_command(UINT op,const void*p,UINT size){if(op!=D9WG_OP_SET_INDICES||size!=sizeof(D9WGSetIndices))return 0;last_handle=((const D9WGSetIndices*)p)->buffer_handle;emits++;return 1;}
${["device_set_indices","device_get_indices"].map(extract).join("\n")}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"D3D8 indices line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){D8Device device={0},other={0};D8IndexBuffer buffer={{&g_ib_vtbl},&device,41,1},foreign={{&g_ib_vtbl},&other,42,1};const UINT bases[]={0,3,0x7fffffffu,0x80000000u,0xffffffffu,0xfffffffdu};
 for(unsigned i=0;i<6;++i){CHECK(device_set_indices(&device,&buffer.iface,bases[i])==0);IDirect3DIndexBuffer8 *got=NULL;UINT base=17;CHECK(device_get_indices(&device,&got,&base)==0&&got==&buffer.iface&&base==bases[i]&&buffer.refs==3);IDirect3DIndexBuffer8_Release(got);CHECK(last_handle==41&&buffer.refs==2);}
 unsigned before=emits;CHECK(device_set_indices(&device,&foreign.iface,UINT32_MAX)==-1&&emits==before&&buffer.refs==2&&foreign.refs==1);IDirect3DIndexBuffer8 *got=NULL;UINT base=17;CHECK(device_get_indices(&device,NULL,&base)==-1&&base==17);CHECK(device_get_indices(&device,&got,NULL)==-1&&!got);
 StateBlock recording={0};device.recording_state_block=&recording;
 for(unsigned i=0;i<6;++i){CHECK(device_set_indices(&device,&buffer.iface,bases[i])==0);CHECK(recording.state.index_buffer==&buffer&&recording.state.indices_mask&&recording.state.base_vertex_index==bases[i]);CHECK(device.base_vertex_index==bases[5]&&emits==before&&buffer.refs==3);}
 CHECK(device_set_indices(&device,NULL,0x80000000u)==0&&!recording.state.index_buffer&&recording.state.base_vertex_index==0x80000000u&&buffer.refs==2);device.recording_state_block=NULL;
 CHECK(device_set_indices(&device,NULL,UINT32_MAX)==0&&last_handle==0&&buffer.refs==1);CHECK(device_get_indices(&device,&got,&base)==0&&!got&&base==UINT32_MAX);
 puts("PASS actual D3D8 Set/GetIndices: six full UINT base patterns, exact retained/query references, foreign/null failures, deferred state-block isolation and null-buffer base retention");return 0;}
`;fs.writeFileSync(dir+"/indices.c",code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,dir+"/indices.c","-o",dir+"/indices"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/indices",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
