"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'device8-methods-')),source=fs.readFileSync(process.env.D8WG_DEVICE_METHODS_CORE_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8');
function extract(name){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{','m').exec(source);assert(m,name);let e=source.indexOf('{',m.index)+1,d=1;for(;d;e++){if(source[e]==='{')d++;if(source[e]==='}')d--;}return source.slice(m.index,e);}
try{fs.writeFileSync(dir+'/methods.c',`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD,ULONG;typedef int BOOL,HRESULT;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DCREATE_PUREDEVICE 0x10u
#define D9_MAX_VS_CONST_F 256u
#define D9_MAX_PS_CONST_F 224u
#define TRACE_REFUSE(x) (x)
#define TRACE(...) ((void)0)
#define CopyMemory memcpy
#define PURE_DEVICE_REFUSE(device,name) do{(void)(name);if(device_is_pure(device))return D3DERR_INVALIDCALL;}while(0)
typedef struct Root{UINT compatibility_api;}Root;
typedef struct IDirect3DDevice9{const void*lpVtbl;Root*parent;struct{DWORD BehaviorFlags;}creation;float vs_const_f[256][4],ps_const_f[224][4],recorded[256][4];BOOL recording;UINT recorded_start,recorded_count;}D9Device;
typedef struct IDirect3DDevice9 IDirect3DDevice9;
typedef struct D9WGDeviceMethods8{UINT marker;}D9WGDeviceMethods8;
static const D9WGDeviceMethods8 g_core_device_methods8={57};static int g_device_vtbl;
static D9Device*device_from_iface(IDirect3DDevice9*d){return d;}
static UINT device_compatibility_api(const D9Device*d){return d->parent->compatibility_api==8?8u:9u;}
static UINT emissions,last_start,last_count,last_bytes,last_op;static unsigned char last_payload[4096];static BOOL fail_emit;
static BOOL emit_shader_constants(D9Device*d,UINT op,UINT start,UINT count,const void*data,UINT bytes){(void)d;emissions++;last_start=start;last_count=count;last_bytes=bytes;last_op=op;if(bytes>sizeof(last_payload))abort();memcpy(last_payload,data,bytes);return !fail_emit;}
static BOOL state_block_record_constant(D9Device*d,BOOL pixel,BOOL floating,BOOL integer,UINT start,UINT count,const void*data){(void)pixel;if(!d->recording)return FALSE;if(!floating||integer)abort();memcpy(d->recorded[start],data,count*16u);d->recorded_start=start;d->recorded_count=count;return TRUE;}
${['device_is_pure','constant_vector_equal','constant_range_valid','set_constant_f','device_set_vertex_shader_constant_f','device_set_pixel_shader_constant_f','device_get_vertex_shader_constant_f','device_get_pixel_shader_constant_f','device_get_available_texture_mem','core_device_set_vertex_shader_constant8','core_device_set_pixel_shader_constant8','core_device_get_vertex_shader_constant8','core_device_get_pixel_shader_constant8','core_device_get_available_texture_mem8','d9wg_d3d9_core_device_methods8'].map(extract).join('\n')}
static UINT checks,cases;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Device methods line %u: %s\\n",__LINE__,#x);return 1;}checks++;}while(0)
static HRESULT set8(D9Device*d,BOOL ps,UINT reg,const float*data,UINT count){return ps?core_device_set_pixel_shader_constant8(d,reg,data,count):core_device_set_vertex_shader_constant8(d,reg,data,count);}
static HRESULT get8(D9Device*d,BOOL ps,UINT reg,float*data,UINT count){return ps?core_device_get_pixel_shader_constant8(d,reg,data,count):core_device_get_vertex_shader_constant8(d,reg,data,count);}
static HRESULT set9(D9Device*d,BOOL ps,UINT reg,const float*data,UINT count){return ps?device_set_pixel_shader_constant_f(d,reg,data,count):device_set_vertex_shader_constant_f(d,reg,data,count);}
static HRESULT get9(D9Device*d,BOOL ps,UINT reg,float*data,UINT count){return ps?device_get_pixel_shader_constant_f(d,reg,data,count):device_get_vertex_shader_constant_f(d,reg,data,count);}
int main(void){
 const uint32_t patterns[]={0u,0x80000000u,0x7fc00001u,0x7fc00002u,0x7f800001u,0x7f800000u,0xff800000u,1u,0x80000001u,0x3f800000u,0xbf800000u};
 for(UINT profile=8;profile<=9;profile++)for(UINT pure=0;pure<2;pure++){
  Root root={profile};D9Device d={.lpVtbl=&g_device_vtbl,.parent=&root,.creation={pure?D3DCREATE_PUREDEVICE:0}};
  CHECK(device_is_pure(&d)==(profile==9&&pure));CHECK(d9wg_d3d9_core_device_methods8(&d)==(profile==8?&g_core_device_methods8:NULL));CHECK(device_get_available_texture_mem(&d)==256u*1024u*1024u);if(profile==8)CHECK(core_device_get_available_texture_mem8(&d)==128u*1024u*1024u);
  if(profile==8){float zero[4]={0},negative[4]={0};uint32_t sign=0x80000000u;memcpy(negative,&sign,4);CHECK(!set8(&d,FALSE,0,zero,1));UINT before=emissions;CHECK(!set8(&d,FALSE,0,negative,1)&&emissions==before+1&&!memcmp(d.vs_const_f[0],negative,16));before=emissions;CHECK(!set8(&d,FALSE,0,zero,1)&&emissions==before+1&&!memcmp(d.vs_const_f[0],zero,16));cases++;}
  for(UINT ps=0;ps<2;ps++){
   UINT limit=profile==8?(ps?8u:96u):(ps?224u:256u);float data[4],out[4];uint32_t bits[4]={0x3f800000u,0x40000000u,0x40400000u,0x40800000u};memcpy(data,bits,16);memset(out,0xa5,16);
   CHECK(set9(&d,ps,0,data,0)==D3DERR_INVALIDCALL&&get9(&d,ps,0,NULL,0)==D3DERR_INVALIDCALL);
   if(profile==8){for(UINT k=0;k<3;k++){UINT reg=k==0?0:k==1?limit:UINT32_MAX;UINT before=emissions;CHECK(!set8(&d,ps,reg,NULL,0)&&!get8(&d,ps,reg,NULL,0)&&emissions==before);cases++;}}
   for(UINT round=0;round<64;round++)for(UINT i=0;i<sizeof(patterns)/sizeof(patterns[0]);i++){
    UINT reg=(round*7u+i)%limit;uint32_t words[4]={patterns[i],patterns[(i+1)%11],patterns[(i+3)%11],patterns[(i+7)%11]};memcpy(data,words,16);const float*before_bank=(ps?d.ps_const_f:d.vs_const_f)[reg];BOOL changed=memcmp(before_bank,data,16)!=0;UINT before=emissions;
    CHECK((profile==8?set8(&d,ps,reg,data,1):set9(&d,ps,reg,data,1))==D3D_OK);CHECK(!memcmp((ps?d.ps_const_f:d.vs_const_f)[reg],words,16));CHECK(emissions==before+(changed?1u:0u));if(changed)CHECK(last_start==reg&&last_count==1&&last_bytes==16&&last_op==(ps?D9WG_OP_SET_PIXEL_SHADER_CONSTANT_F:D9WG_OP_SET_VERTEX_SHADER_CONSTANT_F)&&!memcmp(last_payload,words,16));
    memset(out,0xa5,16);HRESULT hr=profile==8?get8(&d,ps,reg,out,1):get9(&d,ps,reg,out,1);if(profile==9&&pure){CHECK(hr==D3DERR_INVALIDCALL);for(UINT b=0;b<16;b++)CHECK(((unsigned char*)out)[b]==0xa5);}else CHECK(hr==D3D_OK&&!memcmp(out,words,16));
    before=emissions;CHECK((profile==8?set8(&d,ps,reg,data,1):set9(&d,ps,reg,data,1))==D3D_OK&&emissions==before);cases++;
   }
   if(profile==8){for(UINT k=0;k<4;k++){UINT reg=k==0?limit:k==1?limit-1:UINT32_MAX,count=k==1?2:k==3?UINT32_MAX:1;D9Device old=d;UINT before=emissions;memset(out,0xa5,16);CHECK(set8(&d,ps,reg,data,count)==D3DERR_INVALIDCALL&&get8(&d,ps,reg,out,count)==D3DERR_INVALIDCALL&&!memcmp(&d,&old,sizeof(d))&&emissions==before);for(UINT b=0;b<16;b++)CHECK(((unsigned char*)out)[b]==0xa5);cases++;}
    uint32_t words[4]={0x80000000u,0x7fc00003u,0x7f800001u,1u};memcpy(data,words,16);float previous[4];memcpy(previous,(ps?d.ps_const_f:d.vs_const_f)[0],16);UINT before=emissions;d.recording=TRUE;CHECK(!set8(&d,ps,0,data,1)&&emissions==before&&d.recorded_start==0&&d.recorded_count==1&&!memcmp(d.recorded[0],words,16)&&!memcmp((ps?d.ps_const_f:d.vs_const_f)[0],previous,16));CHECK(!get8(&d,ps,0,out,1)&&!memcmp(out,previous,16));d.recording=FALSE;cases++;
    words[0]=0x40a00000u;memcpy(data,words,16);fail_emit=TRUE;CHECK(set8(&d,ps,0,data,1)==D3DERR_DRIVERINTERNALERROR&&!memcmp((ps?d.ps_const_f:d.vs_const_f)[0],words,16));fail_emit=FALSE;CHECK(!get8(&d,ps,0,out,1)&&!memcmp(out,words,16));cases++;
   }
  }
 }
 Root root={8};D9Device bad={.lpVtbl=(void*)123,.parent=&root};CHECK(!d9wg_d3d9_core_device_methods8(NULL)&&!d9wg_d3d9_core_device_methods8(&bad));
 /* A change confined to each byte must be dirty, then identical bits clean. */
 D9Device d={.lpVtbl=&g_device_vtbl,.parent=&root};for(UINT byte=0;byte<16;byte++){float zero[4]={0};CHECK(!set8(&d,FALSE,0,zero,1));float data[4]={0};((unsigned char*)data)[byte]=1;UINT before=emissions;CHECK(!set8(&d,FALSE,0,data,1)&&emissions==before+1&&!memcmp(d.vs_const_f[0],data,16));before=emissions;CHECK(!set8(&d,FALSE,0,data,1)&&emissions==before);cases++;}
 printf("PASS actual core8 device method policies: %u checks / %u cases; 128MiB8/256MiB9,profile/type selector,96VS/8PS bounds andempty calls,legacy8 versusordinary9 pure queries,bit-exact signedzero/NaN/Inf/subnormal CPU+wire,duplicate suppression,recording isolation andcommitted transport failures;five policy thunks+realcoreconstant methods,full57table coveredseparately bySDK/XP\\n",checks,cases);return 0;
}
`);const c=spawnSync(process.env.CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/methods.c','-o',dir+'/methods'],{encoding:'utf8'});assert.equal(c.status,0,c.stderr||String(c.error));const r=spawnSync(dir+'/methods',[],{encoding:'utf8',timeout:30000});assert(!r.error,r.error);assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
