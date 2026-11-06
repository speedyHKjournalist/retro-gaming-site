"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-lockable-target-")),source=fs.readFileSync(process.env.D9WG_LOCKABLE_TARGET_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
try{const old=fs.readFileSync(root+"/tests/fixtures/d3d9_render_target_before_lockable.c","utf8");const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UINT,DWORD;typedef int HRESULT,BOOL,WINBOOL,D3DFORMAT,D3DMULTISAMPLE_TYPE;typedef void*HANDLE;typedef struct{UINT token;}D9Device,IDirect3DDevice9;typedef struct{void*vtbl;BOOL lockable_render_target;UINT canary;}D9Surface,IDirect3DSurface9;
#define WINAPI
#ifndef E_NOTIMPL
#define E_NOTIMPL ((HRESULT)0x80004001)
#endif
#define D3D_OK 0
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(v) ((v)>=0)
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-3)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DMULTISAMPLE_NONE 0
#define D3DUSAGE_RENDERTARGET 1
#define TRACE_REFUSE(v) (v)
static D9Surface storage;static UINT seen[8],calls;static HRESULT provider_error;
static D9Device*device_from_iface(D9Device*d){return d;}static D9Surface*surface_from_iface(D9Surface*s){return s;}
static BOOL supported_multisample(D3DFORMAT f,UINT m){return (m==0||m==4)&&f<100;}
static BOOL supported_render_target_format(D3DFORMAT f){return f>=21&&f<=23;}
static HRESULT create_target_texture(D9Device*d,UINT w,UINT h,D3DFORMAT f,DWORD usage,UINT ms,DWORD quality,D9Surface**out){calls++;UINT args[]={d->token,w,h,(UINT)f,usage,ms,quality};memcpy(seen,args,sizeof(args));if(!out)return -1;*out=NULL;if(!w||!h||w>8192||h>8192)return -1;if(provider_error)return provider_error;storage=(D9Surface){NULL,FALSE,0x1234abcd};*out=&storage;return 0;}
${old.replace(/\bdevice_create_render_target\b/g,'create_before')}
${extract(source,'device_create_render_target')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"lockable target constructor line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){(void)surface_from_iface;const UINT samples[]={0,1,4,16},formats[]={21,22,23,24,111},dims[]={0,4,8192,8193};const WINBOOL lockables[]={0,1,-1};D9Device device={77};
for(UINT m=0;m<4;m++)for(UINT f=0;f<5;f++)for(UINT q=0;q<2;q++)for(UINT l=0;l<3;l++)for(UINT x=0;x<4;x++)for(UINT y=0;y<2;y++)for(UINT failure=0;failure<3;failure++)for(UINT null_out=0;null_out<2;null_out++){
 provider_error=failure==0?0:failure==1?-3:-2;D9Surface*old_out=(void*)(uintptr_t)1,*actual=(void*)(uintptr_t)1;UINT expected_seen[8];memset(seen,0xcc,sizeof(seen));calls=0;storage.lockable_render_target=0x55;HRESULT expected=create_before(&device,dims[x],y?4:0,formats[f],samples[m],q,lockables[l],null_out?NULL:&old_out,NULL);UINT expected_calls=calls;memcpy(expected_seen,seen,sizeof(seen));memset(seen,0xcc,sizeof(seen));calls=0;storage.lockable_render_target=0x55;
 HRESULT hr=device_create_render_target(&device,dims[x],y?4:0,formats[f],samples[m],q,lockables[l],null_out?NULL:&actual,NULL);CHECK(hr==expected&&actual==old_out&&calls==expected_calls&&!memcmp(seen,expected_seen,sizeof(seen)));if(!hr)CHECK(actual==&storage&&storage.lockable_render_target==!!lockables[l]&&storage.canary==0x1234abcd);else CHECK(storage.lockable_render_target==0x55);cases++;
}
printf("PASS actual9 lockable target constructor: %u checks,%u cases; old validation/results/output/provider arguments preserved,TRUE/FALSE publication,MSAA/quality/format/dimensions,NULL/OOM/transport guards\\n",checks,cases);return 0;}
`;fs.writeFileSync(dir+"/ctor.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/ctor.c","-o",dir+"/ctor"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/ctor",[],{encoding:"utf8"});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
