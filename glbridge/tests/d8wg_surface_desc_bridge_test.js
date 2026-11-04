"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-desc-")),source=fs.readFileSync(process.env.D8WG_SURFACE_DESC_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
function sdk(api){const r=spawnSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-P','-x','c','-'],{input:'#include <d3d'+api+'.h>\n',encoding:'utf8',maxBuffer:16<<20});assert.equal(r.status,0,r.stderr);return r.stdout;}
try{const eight=sdk(8),nine=sdk(9),desc=(s,n)=>{const m=/typedef struct _D3DSURFACE_DESC\s*\{[\s\S]*?\}\s*D3DSURFACE_DESC\s*;/.exec(s);assert(m);return m[0].replace('struct _D3DSURFACE_DESC','struct').replace(/\bD3DSURFACE_DESC\b/g,n);},names=['D16','D16_LOCKABLE','D32','D15S1','D24S8','D24X8','D24X4S4'],defines=names.map(n=>{const re=new RegExp('\\bD3DFMT_'+n+'\\s*=\\s*(\\d+)'),a=re.exec(eight),b=re.exec(nine);assert(a&&b&&a[1]===b[1],n);return '#define D3DFMT_'+n+' '+a[1];}).join('\n');
const code=`#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include "${root}/runtime/d9wg_format_layouts.h"
#include "${process.env.D9WG_DEPTH_LAYOUT_HEADER||root+'/runtime/d9wg_depth_surface_layout8.h'}"
typedef uint32_t UINT,DWORD,D3DFORMAT,D3DRESOURCETYPE,D3DPOOL,D3DMULTISAMPLE_TYPE;typedef int32_t BOOL,HRESULT;
${desc(eight,'Desc8')}
${desc(nine,'D3DSURFACE_DESC')}
typedef struct{const void*lpVtbl;}IDirect3DSurface9;typedef struct{UINT api;}Device;typedef struct{IDirect3DSurface9 iface;Device*device;BOOL auto_depth_stencil;}D9Surface;
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DUSAGE_DEPTHSTENCIL 2
#define D9WG_D3D8_SURFACE_DESC_BYTES 32
#define FAILED(v) ((v)<0)
#define CopyMemory memcpy
${defines}
static const int g_surface_vtbl=1;static UINT calls;static HRESULT provider_error;static D3DSURFACE_DESC provider_desc;
static D9Surface*surface_from_iface(IDirect3DSurface9*s){return(D9Surface*)s;}static UINT device_compatibility_api(Device*d){return d->api;}
static HRESULT IDirect3DSurface9_GetDesc(IDirect3DSurface9*s,D3DSURFACE_DESC*out){assert(s&&out);calls++;*out=provider_desc;return provider_error;}
static BOOL legacy_depth(D3DFORMAT f){return f==D3DFMT_D16||f==D3DFMT_D32||f==D3DFMT_D15S1||f==D3DFMT_D24S8||f==D3DFMT_D24X8||f==D3DFMT_D24X4S4;}
static BOOL supported_depth_stencil_format(D3DFORMAT f){return legacy_depth(f)||f==D3DFMT_D16_LOCKABLE;}
${extract(source,'supported_depth_stencil_format8')}
${extract(source,'d9wg_d3d9_core_surface_desc8')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Surface desc bridge line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
static BOOL oracle_size(UINT f,UINT kind,UINT w,UINT h,UINT*size){__uint128_t bytes;if(kind>=2){if(!legacy_depth(f)||!w||!h)return 0;UINT b=f==D3DFMT_D16||f==D3DFMT_D15S1?2:4;bytes=(((__uint128_t)w*b+3)/4*4)*h;}else{const D9WGFormatLayout*l=d9wg_find_format(f);if(!l||!(l->apis&2u))return 0;bytes=(((__uint128_t)w+l->block_width-1)/l->block_width)*l->block_bytes*(((__uint128_t)h+l->block_height-1)/l->block_height);}if(bytes>UINT32_MAX)return 0;*size=(UINT)bytes;return 1;}
int main(void){(void)&IDirect3DSurface9_GetDesc;(void)&supported_depth_stencil_format8;Device good={8},wrong={9};D9Surface surface={{&g_surface_vtbl},&good,0};const UINT widths[]={3,8192,0xffffffffu,0xffffffffu,0},heights[]={2,8192,2,0xffffffffu,0};
_Static_assert(sizeof(Desc8)==32&&sizeof(D3DSURFACE_DESC)==32,"SDK sizes");_Static_assert(offsetof(Desc8,Size)==16&&offsetof(Desc8,MultiSampleType)==20&&offsetof(D3DSURFACE_DESC,MultiSampleType)==16&&offsetof(D3DSURFACE_DESC,MultiSampleQuality)==20,"SDK swapped words");
for(UINT f=0;f<sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)+1;f++)for(UINT kind=0;kind<4;kind++)for(UINT dim=0;dim<5;dim++)for(UINT ms=0;ms<2;ms++)for(UINT quality=0;quality<2;quality++)for(UINT error=0;error<2;error++)for(UINT bad=0;bad<5;bad++){
 const UINT format=f==sizeof(d9wg_format_layouts)/sizeof(*d9wg_format_layouts)?0:d9wg_format_layouts[f].format;provider_desc=(D3DSURFACE_DESC){.Format=format,.Type=1,.Usage=kind>=2?2:kind,.Pool=kind==0?2:0,.MultiSampleType=ms?4:0,.MultiSampleQuality=quality?17:0,.Width=widths[dim],.Height=heights[dim]};surface.auto_depth_stencil=kind==3;surface.iface.lpVtbl=bad==4?NULL:&g_surface_vtbl;provider_error=error?-2:0;calls=0;
 struct Guarded{uint8_t pre[16];Desc8 d;uint8_t post[16];}actual,wanted;memset(&actual,0xcc,sizeof(actual));wanted=actual;UINT bytes=bad==1?31:bad==2?33:32,size=0;BOOL valid=oracle_size(format,kind,widths[dim],heights[dim],&size);HRESULT expected=bad?-1:provider_error?provider_error:valid?0:-1;
 if(!expected)wanted.d=(Desc8){.Format=format,.Type=1,.Usage=provider_desc.Usage,.Pool=provider_desc.Pool,.Size=size,.MultiSampleType=ms?4:0,.Width=widths[dim],.Height=heights[dim]};
 CHECK(d9wg_d3d9_core_surface_desc8(&surface.iface,bad==3?NULL:&actual.d,bytes)==expected);CHECK(calls==!bad);CHECK(!memcmp(&actual,&wanted,sizeof(actual)));cases++;
}
for(UINT bad=0;bad<3;bad++){uint8_t out[32],saved[32];memset(out,0xa5,32);memcpy(saved,out,32);surface.device=bad==2?&wrong:&good;surface.iface.lpVtbl=&g_surface_vtbl;calls=0;CHECK(d9wg_d3d9_core_surface_desc8(bad==0?NULL:&surface.iface,bad==1?NULL:out,32)==-1&&!calls&&!memcmp(out,saved,32));cases++;}
printf("PASS actual SDK8 surface descriptor: %u checks,%u cases; real8/9 32-byte layouts,Size/sample word ordering,CPU/RT/standalone-depth/implicit-depth,128-bit independent size oracle,format/overflow/profile/size/provider failure output guards\\n",checks,cases);return 0;}
`;
fs.writeFileSync(dir+"/desc.c",code);const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/desc.c","-o",dir+"/desc"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+"/desc",[],{encoding:"utf8",timeout:30000});assert.equal(r.status,0,r.stdout+r.stderr+" signal="+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
