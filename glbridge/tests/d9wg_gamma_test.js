"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-gamma-"));
function extract(source,name){const m=new RegExp("static [^;{}]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
try{for(const api of [8,9]){const source=fs.readFileSync(process.env['D'+api+'WG_GAMMA_SOURCE']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),header=process.env.D9WG_GAMMA_HEADER||root+'/runtime/d9wg_gamma_bridge.h',suffix=api===9?'_ramp':'';fs.writeFileSync(dir+'/gamma.c',`#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD,UINT;typedef uint16_t WORD;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define WINAPI
#define TRACE(...) ((void)0)
#define D8WG_TRACE(...) ((void)0)
#define ZeroMemory(p,n) memset(p,0,n)
#define D8WG_MAX_VS_CONSTANTS 2
#define D8WG_MAX_PS_CONSTANTS 2
#define D9_MAX_VS_CONST_F 2
#define D9_MAX_PS_CONST_F 2
#define D9_MAX_CONST_I 2
#define D9_MAX_CONST_B 2
typedef struct{WORD red[256],green[256],blue[256];}D3DGAMMARAMP;
typedef struct{UINT handle;D3DGAMMARAMP gamma_ramp;BOOL gamma_ramp_set;DWORD gamma_ramp_flags;void*recording_state_block;struct{BOOL EnableAutoDepthStencil;}present;DWORD render_states[4];UINT material;float vs_constants[2][4],ps_constants[2][4],vs_const_f[2][4],ps_const_f[2][4];int vs_const_i[2][4],ps_const_i[2][4];BOOL vs_const_b[2],ps_const_b[2];}Device;
typedef Device D${api}Device,IDirect3DDevice${api};
static UINT expected_api=${api};static UINT count,fail;static D9WGSetGammaRamp command;
static BOOL emit_command(uint16_t op,const void*p,uint32_t size){assert(op==D9WG_OP_SET_GAMMA_RAMP&&size==sizeof(command));memcpy(&command,p,size);count++;return !fail;}
static Device*device_from_iface(Device*d){return d;}
${api===9?'static UINT device_compatibility_api(const Device*d){(void)d;return expected_api;}':''}
static BOOL emit_render_default_patches(UINT handle,UINT version,BOOL depth,const DWORD*rs){(void)handle;assert(version==expected_api);(void)depth;(void)rs;return TRUE;}
static BOOL emit_object_default_patches(UINT handle,const UINT*material){(void)handle;(void)material;return TRUE;}
${api===8?'static BOOL emit_set_shader_constant(UINT op,Device*d,UINT first,const float*data,UINT n){(void)op;(void)d;(void)first;(void)data;(void)n;return TRUE;}':'static BOOL emit_shader_constants(Device*d,UINT op,UINT first,UINT n,const void*data,UINT bytes){(void)d;(void)op;(void)first;(void)n;(void)data;(void)bytes;return TRUE;}'}
#include "${header}"
${['device_set_gamma'+suffix,'device_get_gamma'+suffix,'emit_device_state_after_reset'].map(n=>extract(source,n)).join('\n')}
static void set(Device*d,DWORD flags,const D3DGAMMARAMP*r){device_set_gamma${suffix}(d,${api===9?'0,':''}flags,r);}
static void get(Device*d,D3DGAMMARAMP*r){device_get_gamma${suffix}(d,${api===9?'0,':''}r);}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Gamma API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){${api===9?'(void)&device_compatibility_api;':''}Device d={.handle=0x12345678};struct{UINT before;D3DGAMMARAMP ramp;UINT after;}out={.before=0xabcdef01,.after=0x1234abcd};unsigned cases=0;get(&d,&out.ramp);for(UINT i=0;i<256;i++)CHECK(out.ramp.red[i]==i*257&&out.ramp.green[i]==i*257&&out.ramp.blue[i]==i*257);CHECK(!count&&!d.gamma_ramp_set&&out.before==0xabcdef01&&out.after==0x1234abcd);cases++;
 for(UINT n=0;n<16;n++){D3DGAMMARAMP input,expected;for(UINT i=0;i<256;i++){input.red[i]=(WORD)(i*251+n*17);input.green[i]=(WORD)(i*97+n*389+0x2345);input.blue[i]=(WORD)(65535-i*113-n*31);}expected=input;DWORD flags=0x80000000u+n*513;unsigned before=count;fail=n%4==0;d.recording_state_block=(void*)(uintptr_t)(n%2);set(&d,flags,&input);CHECK(count==before+1&&d.gamma_ramp_set&&d.gamma_ramp_flags==flags&&!memcmp(&d.gamma_ramp,&expected,sizeof(expected)));memset(&input,0,sizeof(input));get(&d,&out.ramp);CHECK(!memcmp(&out.ramp,&expected,sizeof(expected))&&out.before==0xabcdef01&&out.after==0x1234abcd);CHECK(command.device_handle==d.handle&&!command.swap_chain&&command.flags==flags&&!command.reserved);for(UINT i=0;i<256;i++)CHECK(command.ramp[i]==expected.red[i]&&command.ramp[256+i]==expected.green[i]&&command.ramp[512+i]==expected.blue[i]);fail=0;set(&d,flags,&expected);CHECK(count==before+2);cases++;}
 unsigned before=count;Device old=d;set(&d,7,NULL);get(&d,NULL);CHECK(count==before&&!memcmp(&old,&d,sizeof(d)));cases++;
${api===9?' device_set_gamma_ramp(&d,1,7,&out.ramp);memset(&out.ramp,0xa5,sizeof(out.ramp));device_get_gamma_ramp(&d,1,&out.ramp);D3DGAMMARAMP zero={0};CHECK(count==before&&!memcmp(&old,&d,sizeof(d))&&!memcmp(&zero,&out.ramp,sizeof(zero)));device_get_gamma_ramp(&d,1,NULL);cases++;':''}
 d.handle=0x87654321;CHECK(emit_device_state_after_reset(&d)&&count==before+1&&command.device_handle==d.handle&&command.flags==d.gamma_ramp_flags);get(&d,&out.ramp);CHECK(!memcmp(&out.ramp,&d.gamma_ramp,sizeof(out.ramp)));cases++;
 before=count;fail=1;CHECK(!emit_device_state_after_reset(&d)&&count==before+1&&d.gamma_ramp_set);fail=0;cases++;
 d.gamma_ramp_set=FALSE;before=count;CHECK(emit_device_state_after_reset(&d)&&count==before);${api===9?'expected_api=8;CHECK(emit_device_state_after_reset(&d)&&count==before);cases++;expected_api=9;':''}get(&d,&out.ramp);for(UINT i=0;i<256;i++)CHECK(out.ramp.red[i]==i*257&&out.ramp.green[i]==i*257&&out.ramp.blue[i]==i*257);cases++;
 printf("PASS actual API ${api} gamma: %u identity, exact 16-bit RGB/flags, copied storage, NULL/chain/recording, failed-submit cache, same-value retry and actual Reset replay cases\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/gamma.c','-o',dir+'/gamma'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/gamma',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr+' signal='+run.signal);process.stdout.write(run.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
