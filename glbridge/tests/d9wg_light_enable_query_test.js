"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),native=require(path.resolve(root,"../../v86/tests/glbridge/fixtures/d3d7_native_light_state.json"));assert(native.cases.some(c=>c.enabled===128));
const header=process.env.D9WG_LIGHT_QUERY_HEADER||path.join(root,"runtime/d9wg_light_state.h"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-enable-queries-"));
function extract(source,name){const m=new RegExp("static HRESULT WINAPI "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
try{for(const api of [7,8,9]){
 const source=fs.readFileSync(process.env['D'+api+'WG_LIGHT_QUERY_SOURCE']||path.join(root,api===7?'ddrawproxy/d3d7_proxy.inc':'d3d'+api+'proxy/d3d'+api+'_proxy.c'),'utf8'),name=api===7?'d3d7_device_GetLightEnable':'device_get_light_enable';
 const code=`#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int BOOL,WINBOOL,HRESULT;typedef uint32_t DWORD;
typedef struct Light {uint32_t words[26];} Light;
#define D9WG_LIGHTING_SDK_LIGHT Light
#define D9WG_LIGHT_ALLOC(n) calloc(1,n)
#define D9WG_LIGHT_FREE(p) free(p)
#include "${header}"
#define WINAPI
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define DDERR_INVALIDPARAMS (-1)
#define D8WG_TRACE_ERROR(x) (x)
#define DDWG_TRACE_ERROR(x) (x)
#define TRACE_REFUSE(x) (x)
#define PURE_DEVICE_REFUSE(...) ((void)0)
#define D8WG_MAX_LIGHTS 8u
#define D9_MAX_LIGHTS 8u
#define D3D7_MAX_LIGHTS 8u
typedef struct Device {BOOL light_set[8],light_enabled[8];D9WGLightState *extra_lights;} Device;
typedef Device D8Device,D9Device,D3D7Device,IDirect3DDevice${api};
static Device *${api===7?'d3d7_device_from_iface':'device_from_iface'}(Device *p){return p;}
${extract(source,name)}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"light query API ${api} line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
static const struct Native {int known,active;uint32_t expected;} native[]={${native.cases.map(r=>'{'+(r.enable===0?1:0)+','+(r.enabled===128?1:0)+','+r.enabled+'u}').join(',')}};
int main(void){Device device={0};const DWORD keys[]={0,7,8,37,1000,0xffffffffu};const BOOL inputs[]={0,1,7,-1,128};
 for(unsigned i=0;i<sizeof(native)/sizeof(native[0]);++i){BOOL value=(BOOL)0xccccccccu;int ok=d9wg_light_query_enable(native[i].known,native[i].active,&value);CHECK(ok==native[i].known&&(uint32_t)value==native[i].expected);}
 for(unsigned i=0;i<6;++i){DWORD index=keys[i];BOOL value=(BOOL)0xccccccccu;CHECK(${name}(&device,index,&value)==-1&&(uint32_t)value==0xccccccccu);CHECK(${name}(&device,index,NULL)==-1);
  if(${api}==7&&index>=8)continue;D9WGLightState *entry=NULL;if(index>=8){entry=d9wg_light_get(&device.extra_lights,index,1);CHECK(entry);entry->enabled=1;value=(BOOL)0xccccccccu;CHECK(${name}(&device,index,&value)==-1&&(uint32_t)value==0xccccccccu);entry->set=1;}else device.light_set[index]=1;
  for(unsigned j=0;j<5;++j){if(entry)entry->enabled=inputs[j];else device.light_enabled[index]=inputs[j];CHECK(${name}(&device,index,&value)==0&&value==(inputs[j]?128:0));}
  if(entry)entry->set=0;else device.light_set[index]=0;value=(BOOL)0xccccccccu;CHECK(${name}(&device,index,&value)==-1&&(uint32_t)value==0xccccccccu);
 }d9wg_light_free(&device.extra_lights);return 0;}
`;
 const input=path.join(directory,'query'+api+'.c'),binary=path.join(directory,'query'+api);fs.writeFileSync(input,code);const built=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror',input,'-o',binary],{encoding:'utf8'});assert.equal(built.status,0,built.stderr||String(built.error));const run=spawnSync(binary,[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr||String(run.error));
 }console.log('PASS actual D3D7/8/9 light-enable queries: frozen native cases, bit-seven enabled BOOL, noncanonical inputs, unknown/allocated-unset indices, null and untouched failure outputs');
}finally{fs.rmSync(directory,{recursive:true,force:true});}
