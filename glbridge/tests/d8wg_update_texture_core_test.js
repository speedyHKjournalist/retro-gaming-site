"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"update8-core-")),s8=fs.readFileSync(process.env.D8WG_UPDATE_SOURCE8||root+"/d3d8proxy/d3d8_proxy.c","utf8"),s9=fs.readFileSync(process.env.D8WG_UPDATE_SOURCE9||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
function family(s,names,prefix){let code=names.map(n=>extract(s,n)).join("\n");for(const n of names)code=code.replace(new RegExp("\\b"+n+"\\b","g"),prefix+n);return code;}
try{for(const api of[8,9]){const sdk=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P","-x","c","-"],{input:'#include <d3d'+api+'.h>\n',encoding:'utf8',maxBuffer:16<<20});assert.equal(sdk.status,0,sdk.stderr);for(const[name,id]of Object.entries({D3DPOOL_DEFAULT:0,D3DPOOL_MANAGED:1,D3DPOOL_SYSTEMMEM:2,D3DPOOL_SCRATCH:3}))assert.equal(Number(new RegExp('\\b'+name+'\\s*=\\s*(\\d+)').exec(sdk.stdout)?.[1]),id,name);}
 const names8=['update_cube_texture','update_volume_texture','device_update_texture'],code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "${root}/runtime/d9wg_runtime.h"
#include "${process.env.D8WG_UPDATE_POLICY_HEADER||root+'/runtime/d9wg_texture_copy_policy.h'}"
typedef uint32_t UINT,DWORD;typedef int32_t HRESULT,BOOL;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define D3DPOOL_DEFAULT 0
#define D3DPOOL_SYSTEMMEM 2
typedef struct {const void*lpVtbl;}Iface,IDirect3DBaseTexture8,IDirect3DBaseTexture9,IDirect3DBaseTexture8Vtbl;
typedef struct Device{UINT api;} Device,D8Device,D9Device,IDirect3DDevice8,IDirect3DDevice9;
typedef struct Texture{Iface iface;Device*device;UINT width,height,depth,edge_length,level_count,format,pool;void*levels;}Texture,D8Texture,D8CubeTexture,D8VolumeTexture,D9Texture,D9CubeTexture,D9VolumeTexture,IDirect3DCubeTexture8,IDirect3DVolumeTexture8;
static const UINT g_texture_vtbl=1,g_cube_texture_vtbl=2,g_cube_vtbl=3,g_volume_texture_vtbl=4,unknown_vtbl=5;
static Device*device_from_iface(Device*d){return d;}static UINT device_compatibility_api(Device*d){return d->api;}
enum{D9WG_COPY_2D,D9WG_COPY_CUBE,D9WG_COPY_VOLUME};
typedef struct{UINT calls,kind;void*texture;const void*source;void*destination;D9WGTextureCopyExtent from,to;}Capture;static Capture captured;static HRESULT injected;
static HRESULT d9wg_update_texture_common(void*t,UINT kind,const void*s,D9WGTextureCopyExtent from,void*d,D9WGTextureCopyExtent to){captured=(Capture){.calls=captured.calls+1,.kind=kind,.texture=t,.source=s,.destination=d,.from=from,.to=to};return injected;}
${family(fs.readFileSync(root+'/tests/fixtures/d3d8_update_before_core.c','utf8'),names8,'old8_')}
${family(fs.readFileSync(root+'/tests/fixtures/d3d9_update_before_core.c','utf8'),['device_update_texture'],'old9_')}
${family(s8,names8,'now8_')}
${family(s9,['device_update_texture'],'now9_')}
#define IDirect3DDevice9_UpdateTexture now9_device_update_texture
${extract(s9,'d9wg_d3d9_core_update_texture8')}
static UINT cases,checks,legacy_wrong_pool_dispatches,public8,ordinary9,core8;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Update8 core case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
int main(void){Device device,foreign={.api=8};const UINT pools[]={0,1,2,3,4,0xffffffffu};const HRESULT results[]={0,-2,-3,(HRESULT)0x80004005u};UINT source_storage[4]={1,2,3,4},destination_storage[2]={5,6};
for(UINT profile=0;profile<3;profile++)for(UINT sk=0;sk<4;sk++)for(UINT dk=0;dk<4;dk++)for(UINT sp=0;sp<6;sp++)for(UINT dp=0;dp<6;dp++)for(UINT parents=0;parents<4;parents++)for(UINT mismatch=0;mismatch<2;mismatch++)for(UINT status=0;status<4;status++)for(UINT nulls=0;nulls<4;nulls++){
 device.api=profile==1?9:8;const void*vt[]={&g_texture_vtbl,profile==0?&g_cube_texture_vtbl:&g_cube_vtbl,&g_volume_texture_vtbl,&unknown_vtbl};Texture a={.iface={vt[sk]},.device=parents&1?&foreign:&device,.width=13,.height=7,.depth=3,.edge_length=13,.level_count=4,.format=21,.pool=pools[sp],.levels=source_storage},b={.iface={vt[dk]},.device=parents&2?&foreign:&device,.width=3,.height=1,.depth=1,.edge_length=3,.level_count=2,.format=mismatch?22:21,.pool=pools[dp],.levels=destination_storage},aa=a,bb=b;Iface*src=nulls&1?NULL:&a.iface,*dst=nulls&2?NULL:&b.iface;BOOL shape=sk==dk&&sk<3&&!parents&&!mismatch&&!nulls,valid=shape&&pools[sp]==2&&pools[dp]==0;injected=results[status];captured=(Capture){0};HRESULT hr=profile==0?now8_device_update_texture(&device,src,dst):profile==1?now9_device_update_texture(&device,src,dst):d9wg_d3d9_core_update_texture8(&device,src,dst);CHECK(hr==(valid?injected:-1)&&captured.calls==(UINT)valid);Capture actual=captured;CHECK(!memcmp(&a,&aa,sizeof(a))&&!memcmp(&b,&bb,sizeof(b)));
 if(valid){D9WGTextureCopyExtent from={sk==1?a.edge_length:a.width,sk==1?a.edge_length:a.height,sk==2?a.depth:1,a.level_count},to={sk==1?b.edge_length:b.width,sk==1?b.edge_length:b.height,sk==2?b.depth:1,b.level_count};CHECK(actual.kind==sk&&actual.texture==&b&&actual.source==a.levels&&actual.destination==b.levels&&!memcmp(&actual.from,&from,sizeof(from))&&!memcmp(&actual.to,&to,sizeof(to)));}
 captured=(Capture){0};HRESULT before=profile==0?old8_device_update_texture(&device,src,dst):old9_device_update_texture(&device,src,dst);CHECK(before==((profile==0?shape:valid)?injected:-1)&&captured.calls==(UINT)(profile==0?shape:valid));if(profile&&valid)CHECK(!memcmp(&actual,&captured,sizeof(actual)));if(!profile&&shape&&!valid)legacy_wrong_pool_dispatches++;
 cases++;if(profile==0)public8++;else if(profile==1)ordinary9++;else core8++;
}
/* NULL/wrong-profile guards must precede any downstream lookup. */
device.api=9;captured=(Capture){0};CHECK(d9wg_d3d9_core_update_texture8(&device,NULL,NULL)==-1&&!captured.calls);CHECK(d9wg_d3d9_core_update_texture8(NULL,NULL,NULL)==-1&&!captured.calls);cases+=2;
printf("PASS UpdateTexture shared policy/private8: %u checks,%u cases (%u public8/%u ordinary9/%u core8);%u actualold8 wrong-pool dispatches blocked;SDK pool enums,2D/cube/volume/type/format/parents/null/pool bounds,unaltered inputs,exact extents/pointers/status andactualold9 oracle\\n",checks,cases,public8,ordinary9,core8,legacy_wrong_pool_dispatches);return 0;}
`;fs.writeFileSync(dir+'/update.c',code);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror',dir+'/update.c','-o',dir+'/update'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/update',[],{encoding:'utf8',timeout:180000});assert.equal(run.status,0,run.stdout+run.stderr+' signal='+run.signal);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
