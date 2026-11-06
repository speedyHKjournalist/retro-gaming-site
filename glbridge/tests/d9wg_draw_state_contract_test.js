'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-draw-state-'));
function extract(source,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return source.slice(m.index,end);}
try {
 const source=fs.readFileSync(process.env.D9WG_DRAW_STATE_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8');
 const code=`#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef int32_t HRESULT,LONG;typedef uint32_t UINT,DWORD;typedef int BOOL;
typedef struct {DWORD X,Y,Width,Height;float MinZ,MaxZ;}D3DVIEWPORT9;
typedef struct {UINT width,height;}D9Surface;
typedef struct {UINT handle,api;struct{UINT frequency;}streams[16];D9Surface*render_target_surfaces[4];struct{UINT Width,Height;}display_mode;D3DVIEWPORT9 viewport;}D9Device,IDirect3DDevice9;
#define WINAPI
#define D9_MAX_STREAMS 16
#define D3DSTREAMSOURCE_INDEXEDDATA 0x40000000u
#define D3DSTREAMSOURCE_INSTANCEDATA 0x80000000u
#define D9_STREAMSOURCE_FREQUENCY_MASK 0x3fffffffu
#define D3DERR_INVALIDCALL -1
#define D3DERR_DRIVERINTERNALERROR -2
#define D3D_OK 0
#define FALSE 0
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
static UINT emitted,recorded;static BOOL record_mode,fail;static D9WGSetStreamSourceFreq wire;
static D9Device*device_from_iface(IDirect3DDevice9*p){return p;}
static UINT device_compatibility_api(D9Device*p){return p->api;}
static BOOL state_block_record_stream_frequency(D9Device*d,UINT s,UINT v){(void)d;(void)s;(void)v;if(record_mode)recorded++;return record_mode;}
static BOOL state_block_record_viewport(D9Device*d,const D3DVIEWPORT9*v){(void)d;(void)v;if(record_mode)recorded++;return record_mode;}
static HRESULT device_set_live_viewport(D9Device*d,const D3DVIEWPORT9*v,BOOL force){(void)force;emitted++;if(fail)return -2;d->viewport=*v;return 0;}
static int emit_command(UINT op,const void*p,UINT n){assert(op==D9WG_OP_SET_STREAM_SOURCE_FREQ&&n==sizeof(wire));wire=*(const D9WGSetStreamSourceFreq*)p;emitted++;return !fail;}
${extract(source,'device_set_stream_source_freq')}
${extract(source,'device_set_viewport')}
int main(void){UINT checks=0;
for(UINT stream=0;stream<18;stream++)for(UINT flags=0;flags<4;flags++)for(UINT f=0;f<4;f++)for(UINT recording=0;recording<2;recording++)for(UINT broken=0;broken<2;broken++){
 D9Device d={.handle=7};for(UINT i=0;i<16;i++)d.streams[i].frequency=1;
 UINT freq=f==3?0x3fffffffu:f,divider=(flags<<30)|freq;
 BOOL accepted=stream<16&&divider&&flags!=3&&!(stream==0&&flags==2);
 emitted=recorded=0;record_mode=recording;fail=broken;
 HRESULT hr=device_set_stream_source_freq(&d,stream,divider);
 BOOL sends=accepted&&!recording&&divider!=1;
 assert(hr==(accepted?(sends&&broken?-2:0):-1));assert(emitted==(UINT)sends&&recorded==(UINT)(accepted&&recording));
 if(stream<16)assert(d.streams[stream].frequency==(accepted&&!recording&&!broken?divider:1));
 if(sends)assert(wire.device_handle==7&&wire.stream==stream&&wire.divider==divider);
 checks+=4;
}
float ranges[][2]={{0,1},{0,0},{.5f,0},{1,1},{-1,2},{-2,-3},{3,2},{NAN,1},{0,NAN},{INFINITY,1},{0,INFINITY},{-INFINITY,1},{0,-INFINITY}};
for(UINT api=8;api<=9;api++)for(UINT i=0;i<sizeof(ranges)/sizeof(*ranges);i++)for(UINT recording=0;recording<2;recording++)for(UINT broken=0;broken<2;broken++)for(UINT large=0;large<2;large++){
 D9Device d={.handle=7,.api=api,.display_mode={64,48}};D3DVIEWPORT9 v={0,0,large?128:64,48,ranges[i][0],ranges[i][1]};
 BOOL accepted=isfinite(v.MinZ)&&isfinite(v.MaxZ)&&!(api==8&&large);
 emitted=recorded=0;record_mode=recording;fail=broken;
 HRESULT hr=device_set_viewport(&d,&v);assert(hr==(accepted?(!recording&&broken?-2:0):-1));
 assert(emitted==(UINT)(accepted&&!recording)&&recorded==(UINT)(accepted&&recording));
 if(accepted&&!recording&&!broken)assert(!memcmp(&v,&d.viewport,sizeof(v)));
 checks+=3;
}
printf("PASS native draw state contracts: %u checks; legal stream modes/zero divisors, rejected modes and atomic transport failure; raw finite viewport endpoints, recording and API8 target bounds\\n",checks);}
`;
 fs.writeFileSync(dir+'/test.c',code);const cc=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(cc.status,0,cc.stderr);
 const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stdout+run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
