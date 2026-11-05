"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"depth-desc8-")),source=readComparisonSource(process.env.D8WG_DEPTH_DESC_SOURCE||root+"/d3d8proxy/d3d8_proxy.c","utf8");
function extract(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return s.slice(m.index,b);}
try{const oldlevel=readComparisonSource(root+'/tests/fixtures/d3d8_depth_level_desc_before_size.c','utf8'),oldsurface=extract(readComparisonSource(root+'/tests/fixtures/d3d8_image_surface_before_core.c','utf8'),'create_standalone_surface');const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "${process.env.D9WG_DEPTH_LAYOUT_HEADER||root+'/runtime/d9wg_depth_surface_layout8.h'}"
typedef uint32_t UINT,DWORD,D3DFORMAT,D3DPOOL;typedef int32_t BOOL,LONG,HRESULT;typedef uint8_t BYTE;
typedef struct{void*lpVtbl;}Iface,IDirect3DTexture8,IDirect3DSurface8;typedef struct{UINT pins,implicit_surface_count;}D8Device;
typedef struct{UINT Format,Type,Usage,Pool,Size,MultiSampleType,Width,Height;}D3DSURFACE_DESC;
typedef struct{UINT width,height,byte_count;}D8TextureLevel;
typedef struct{Iface iface;D3DFORMAT format;UINT level_count,usage,pool,multisample_type;D8TextureLevel*levels;}D8Texture,D8CubeTexture;
typedef struct{Iface iface;LONG refcount;D8Device*device;D8Texture*texture;D8CubeTexture*cube;UINT level;BYTE*shadow;UINT row_pitch;BOOL backbuffer,depth_stencil,reset_blocker;D3DSURFACE_DESC desc;}D8Surface;
#define WINAPI
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-3)
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DMULTISAMPLE_NONE 0
#define D3DRTYPE_SURFACE 1
#define D3DPOOL_DEFAULT 0
#define TRUE 1
#define FALSE 0
#define D8WG_TRACE_ERROR(v) (v)
#define HEAP_ZERO_MEMORY 8
#define ZeroMemory(p,n) memset(p,0,n)
#define GetProcessHeap() NULL
static int g_surface_vtbl;static UINT live;
static void*HeapAlloc(void*h,UINT flags,size_t n){(void)h;(void)flags;void*p=calloc(1,n);assert(p);live++;return p;}static BOOL HeapFree(void*h,UINT flags,void*p){(void)h;(void)flags;assert(p&&live);free(p);live--;return TRUE;}
static void device_child_add_ref(D8Device*d){d->pins++;}
static D8Texture*texture_from_iface(Iface*i){return(D8Texture*)i;}static D8Surface*surface_from_iface(Iface*i){return(D8Surface*)i;}
static BOOL texture_format_layout(D3DFORMAT f,UINT*w,UINT*h,UINT*b){return d9wg_format_layout(8,f,w,h,b);}static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){return d9wg_texture_level_layout(8,f,w,h,p,r,b);}
static HRESULT cube_get_level_desc(Iface*t,UINT l,D3DSURFACE_DESC*d){(void)t;(void)l;(void)d;return -9;}
${oldlevel.replace(/\btexture_get_level_desc\b/g,'level_before')}
${oldsurface.replace(/\bcreate_standalone_surface\b/g,'surface_before')}
${['texture_get_level_desc','surface_get_desc','create_standalone_surface'].map(n=>extract(source,n)).join('\n')}
static UINT checks,cases;
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Depth surface desc8 line %u: %s\\n",__LINE__,#v);return 1;}checks++;}while(0)
int main(void){const UINT formats[]={71,73,75,77,79,80},dims[]={1,3,4,8192};D8Device device={0};
for(UINT fi=0;fi<6;fi++)for(UINT wi=0;wi<4;wi++)for(UINT hi=0;hi<4;hi++)for(UINT sample=0;sample<2;sample++){
 UINT f=formats[fi],w=dims[wi],h=dims[hi],b=f==73||f==80?2:4,pitch=(w*b+3)/4*4,size=pitch*h;D8TextureLevel level={w,h,0};D8Texture texture={.format=f,.level_count=1,.usage=2,.pool=0,.multisample_type=sample?4:0,.levels=&level};D8Surface view={.texture=&texture};D3DSURFACE_DESC old,actual,via_surface;
 CHECK(!level_before(&texture.iface,0,&old)&&old.Size==0);CHECK(!texture_get_level_desc(&texture.iface,0,&actual)&&!surface_get_desc(&view.iface,&via_surface)&&!memcmp(&actual,&via_surface,sizeof(actual)));CHECK(actual.Size==size&&actual.Format==f&&actual.MultiSampleType==(sample?4u:0u)&&actual.Usage==2&&actual.Pool==0&&actual.Width==w&&actual.Height==h);
 Iface*before=NULL,*now=NULL;CHECK(!surface_before(&device,w,h,f,2,0,FALSE,FALSE,TRUE,TRUE,&before)&&!create_standalone_surface(&device,w,h,f,2,0,FALSE,FALSE,TRUE,TRUE,&now));D8Surface*previous=(D8Surface*)before,*current=(D8Surface*)now;CHECK(previous->desc.Size==w*h*4&&current->desc.Size==size&&current->row_pitch==pitch);CHECK(!surface_get_desc(now,&actual)&&actual.Size==size&&actual.Width==w&&actual.Height==h&&current->reset_blocker&&device.implicit_surface_count==2&&device.pins==2);HeapFree(NULL,0,previous);HeapFree(NULL,0,current);device.pins=device.implicit_surface_count=0;cases++;
}
D3DSURFACE_DESC out,before;memset(&out,0xcc,sizeof(out));before=out;D8TextureLevel level={3,2,0};D8Texture bad={.format=70,.level_count=1,.usage=2,.levels=&level};CHECK(texture_get_level_desc(&bad.iface,0,&out)==-1&&!memcmp(&out,&before,sizeof(out)));CHECK(texture_get_level_desc(&bad.iface,1,&out)==-1&&!memcmp(&out,&before,sizeof(out)));CHECK(texture_get_level_desc(&bad.iface,0,NULL)==-1);CHECK(!live&&!device.pins&&!device.implicit_surface_count);
printf("PASS actual public8 depth descriptors: %u checks,%u cases; frozenzero/fixed4 old metadata versusformatawarealignedsize,GetLevelDesc/GetDesc,implicit creation,all6formats,odd/boundary extents,MSAA0/4,identity fields andfailure atomicity\\n",checks,cases);return 0;}
`;fs.writeFileSync(dir+'/desc.c',code);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror',dir+'/desc.c','-o',dir+'/desc'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/desc',[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
