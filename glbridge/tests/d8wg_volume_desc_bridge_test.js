"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-volume-desc-")),source=fs.readFileSync(process.env.D8WG_BACKEND_VOLUME_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,n);let b=source.indexOf("{",m.index)+1,d=1;for(;d;b++){if(source[b]==="{")d++;if(source[b]==="}")d--;}return source.slice(m.index,b);}
function sdk(api,name){const r=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P","-x","c","-"],{input:"#include <d3d"+api+".h>\n",encoding:"utf8",maxBuffer:16<<20});assert.equal(r.status,0,r.stderr);const m=/typedef struct _D3DVOLUME_DESC\s*\{[\s\S]*?\}\s*D3DVOLUME_DESC\s*;/.exec(r.stdout);assert(m);return m[0].replace("struct _D3DVOLUME_DESC","struct").replace(/\bD3DVOLUME_DESC\b/g,name);}
try{const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include "${root}/runtime/d9wg_format_layouts.h"
typedef uint32_t UINT,DWORD,D3DFORMAT,D3DRESOURCETYPE,D3DPOOL;typedef int32_t BOOL,HRESULT;
${sdk(8,'Desc8')}
${sdk(9,'D3DVOLUME_DESC')}
typedef struct{const void*lpVtbl;}IDirect3DVolume9;typedef struct{UINT api;}Device;typedef struct{Device*device;}Texture;typedef struct{IDirect3DVolume9 iface;Texture*texture;}D9Volume;
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D9WG_D3D8_VOLUME_DESC_BYTES 32
#define FAILED(v) ((v)<0)
#define CopyMemory memcpy
#define FALSE 0
#define D3DFMT_DXT1 827611204u
#define D3DFMT_DXT2 844388420u
#define D3DFMT_DXT3 861165636u
#define D3DFMT_DXT4 877942852u
#define D3DFMT_DXT5 894720068u
static const int g_volume_vtbl=1;static UINT calls;static HRESULT provider_error;static D3DVOLUME_DESC provider_desc;
static D9Volume*volume_from_iface(IDirect3DVolume9*v){return(D9Volume*)v;}static UINT device_compatibility_api(Device*d){return d->api;}
static HRESULT IDirect3DVolume9_GetDesc(IDirect3DVolume9*v,D3DVOLUME_DESC*out){assert(v&&out);calls++;*out=provider_desc;return provider_error;}
${['supported_format8','supported_volume_texture_format8','core_pack_volume_desc8','d9wg_d3d9_core_volume_desc8'].map(extract).join('\n')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Volume desc bridge case %u line %u: %s\\n",cases,__LINE__,#v);return 1;}checks++;}while(0)
static BOOL oracle(UINT f,UINT w,UINT h,UINT depth,UINT*bytes){const D9WGFormatLayout*q=d9wg_find_format(f);if(!w||!h||!depth||!q||!(q->apis&2u)||q->block_height>1)return 0;__uint128_t n=(((__uint128_t)w+q->block_width-1)/q->block_width)*q->block_bytes*h*depth;if(n>UINT32_MAX)return 0;*bytes=(UINT)n;return 1;}
int main(void){Device good={8},wrong={9};Texture t={&good};D9Volume volume={{&g_volume_vtbl},&t};const UINT dims[][3]={{3,2,3},{1,1,1},{2048,2048,2048},{UINT32_MAX,2,2},{UINT32_MAX,UINT32_MAX,UINT32_MAX},{0,2,3},{3,0,3},{3,2,0}};
_Static_assert(sizeof(Desc8)==32&&sizeof(D3DVOLUME_DESC)==28,"SDK volume sizes");_Static_assert(offsetof(Desc8,Size)==16&&offsetof(Desc8,Width)==20&&offsetof(Desc8,Depth)==28&&offsetof(D3DVOLUME_DESC,Width)==16&&offsetof(D3DVOLUME_DESC,Depth)==24,"SDK inserted Size word");
for(UINT f=0;f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)+1;f++)for(UINT dim=0;dim<8;dim++)for(UINT pool=0;pool<4;pool++)for(UINT error=0;error<2;error++)for(UINT bad=0;bad<7;bad++){
 UINT format=f==sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)?0:d9wg_format_layouts[f].format,w=dims[dim][0],h=dims[dim][1],depth=dims[dim][2],size=0;provider_desc=(D3DVOLUME_DESC){.Format=format,.Type=2,.Usage=0x20000,.Pool=pool,.Width=w,.Height=h,.Depth=depth};volume.iface.lpVtbl=bad==4?NULL:&g_volume_vtbl;volume.texture=bad==5?NULL:&t;t.device=bad==6?&wrong:&good;calls=0;provider_error=error?-2:0;
 struct Guard{BYTE_PLACEHOLDER pre[16];Desc8 desc;BYTE_PLACEHOLDER post[16];}actual,wanted;memset(&actual,0xcc,sizeof(actual));wanted=actual;UINT n=bad==1?31:bad==2?33:32;BOOL valid=oracle(format,w,h,depth,&size);HRESULT expected=bad?-1:provider_error?provider_error:valid?0:-1;if(!expected)wanted.desc=(Desc8){format,2,0x20000,pool,size,w,h,depth};
 CHECK(d9wg_d3d9_core_volume_desc8(&volume.iface,bad==3?NULL:&actual.desc,n)==expected);CHECK(calls==!bad);CHECK(!memcmp(&actual,&wanted,sizeof(actual)));cases++;
}
uint8_t out[32],saved[32];memset(out,0xa5,32);memcpy(saved,out,32);calls=0;CHECK(d9wg_d3d9_core_volume_desc8(NULL,out,32)==-1&&!calls&&!memcmp(out,saved,32));cases++;
printf("PASS actual SDK8 volume descriptor: %u checks,%u cases;real8/9 32/28-byte layouts,inserted Size word,128-bit row/slice/depth arithmetic,format/zero/overflow/profile/size/provider atomic guards\\n",checks,cases);return 0;}
`.replaceAll("BYTE_PLACEHOLDER","uint8_t");fs.writeFileSync(dir+"/desc.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/desc.c","-o",dir+"/desc"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/desc",[],{encoding:"utf8",timeout:60000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
