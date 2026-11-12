"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_VIEWPORT_SOURCE||root+"/ddrawproxy/d3d_legacy_proxy.inc","utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-viewport-"));
function extract(name){const match=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(match,name);let end=source.indexOf("{",match.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(match.index,end);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;
typedef struct{DWORD dwSize,dwX,dwY,dwWidth,dwHeight;float dvScaleX,dvScaleY,dvMaxX,dvMaxY,dvMinZ,dvMaxZ;}D3DVIEWPORT;
typedef struct{DWORD dwSize,dwX,dwY,dwWidth,dwHeight;float dvClipX,dvClipY,dvClipWidth,dvClipHeight,dvMinZ,dvMaxZ;}D3DVIEWPORT2;
typedef struct DDSurface{void*vtbl7;DWORD width,height;}DDSurface,IDirectDrawSurface7;
typedef struct DDrawObject{void*vtbl7;LONG ref,lifetime;}DDrawObject,IDirectDraw7;
typedef struct D3DLegacyViewport D3DLegacyViewport;
typedef struct{unsigned creation_version;DDSurface*render_target;D3DLegacyViewport*legacy_current_viewport,*legacy_viewports;}D3D7Device;
typedef struct D3DLegacyLight{D3DLegacyViewport*viewport;struct D3DLegacyLight*next_in_viewport;}D3DLegacyLight,IDirect3DLight;
struct D3DLegacyViewport{const int*vtbl3,*vtbl2,*vtbl1;LONG ref;DDrawObject*owner;D3D7Device*device;D3DVIEWPORT viewport1;D3DVIEWPORT2 viewport2;BOOL viewport1_set,viewport2_set;DDSurface*background_depth;D3DLegacyLight*lights;D3DLegacyViewport*next_in_device;};
typedef D3DLegacyViewport IDirect3DViewport3;typedef void IUnknown;
static const int g_d3d_viewport3_vtbl=3,g_d3d_viewport2_vtbl=2,g_d3d_viewport1_vtbl=1;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_INVALIDCAPS (-2)
#define D3DERR_VIEWPORTDATANOTSET (-3)
#define D3DERR_VIEWPORTHASNODEVICE (-4)
#define CLASS_E_NOAGGREGATION (-5)
#define DDERR_OUTOFMEMORY (-6)
#define DDWG_TRACE_ERROR(h) (h)
#define FAILED(h) ((h)<0)
static unsigned checks,applies,fail_alloc;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"viewport line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static D3DLegacyViewport*d3d_viewport_from_any(void*p){return p;}
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){return --*p;}
static void*heap_alloc_zero(size_t n){return fail_alloc?NULL:calloc(1,n);}static void heap_free(void*p){free(p);}
static void ddraw_child_attach(DDrawObject*d,BOOL public_ref){MUST(!public_ref);d->lifetime++;}
static void ddraw_child_detach(DDrawObject*d,BOOL public_ref){MUST(!public_ref);d->lifetime--;MUST(d->lifetime>=d->ref);}
static ULONG surface_Release(IDirectDrawSurface7*s){(void)s;MUST(0);return 0;}
static ULONG d3d_light_Release(IDirect3DLight*l){(void)l;MUST(0);return 0;}
static HRESULT d3d_legacy_apply_viewport(D3DLegacyViewport*v){MUST(v->device);applies++;return D3D_OK;}
${["d3d_viewport_AddRef","d3d_viewport_Release","d3d_legacy_delete_viewport","d3d_legacy_create_viewport","d3d_legacy_viewport_bounds","d3d_viewport_GetViewport","d3d_viewport_SetViewport","d3d_viewport_GetViewport2","d3d_viewport_SetViewport2"].map(extract).join("\n")}
static void owner_cases(void){DDrawObject owner={.ref=1,.lifetime=1};D3DLegacyViewport*v=(void*)1;MUST(d3d_legacy_create_viewport(&owner,&v,(IUnknown*)1)==CLASS_E_NOAGGREGATION&&!v&&owner.ref==1&&owner.lifetime==1);fail_alloc=1;MUST(d3d_legacy_create_viewport(&owner,&v,NULL)==DDERR_OUTOFMEMORY&&!v&&owner.lifetime==1);fail_alloc=0;MUST(d3d_legacy_create_viewport(&owner,&v,NULL)==D3D_OK&&v&&v->ref==1&&owner.ref==1&&owner.lifetime==2);MUST(d3d_viewport_AddRef(v)==2&&d3d_viewport_Release(v)==1);owner.ref--;owner.lifetime--;MUST(!owner.ref&&owner.lifetime==1);MUST(!d3d_viewport_Release(v)&&!owner.lifetime);}
static void contracts(void){D3DLegacyViewport v={0};D3DVIEWPORT one={.dwSize=sizeof(one)},saved={0};D3DVIEWPORT2 two={.dwSize=sizeof(two)};D3D7Device device={.creation_version=1};DDSurface target={.width=640,.height=480};
 MUST(d3d_legacy_delete_viewport(&device,NULL)==DDERR_INVALIDPARAMS);MUST(d3d_legacy_delete_viewport(&device,&v)==D3DERR_VIEWPORTHASNODEVICE);
 MUST(d3d_viewport_GetViewport(&v,&one)==D3DERR_VIEWPORTDATANOTSET&&one.dwSize==sizeof(one));MUST(d3d_viewport_GetViewport2(&v,&two)==D3DERR_VIEWPORTDATANOTSET);
 MUST(d3d_viewport_SetViewport(&v,&one)==D3DERR_VIEWPORTHASNODEVICE&&!v.viewport1_set);MUST(d3d_viewport_SetViewport2(&v,&two)==D3DERR_VIEWPORTHASNODEVICE&&!v.viewport2_set);
 const DWORD sizes[]={0,1,sizeof(one)-1,sizeof(one)+1,1024};for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++){one.dwSize=two.dwSize=sizes[i];MUST(d3d_viewport_GetViewport(&v,&one)==DDERR_INVALIDPARAMS);MUST(d3d_viewport_GetViewport2(&v,&two)==DDERR_INVALIDPARAMS);MUST(d3d_viewport_SetViewport(&v,&one)==DDERR_INVALIDPARAMS);MUST(d3d_viewport_SetViewport2(&v,&two)==DDERR_INVALIDPARAMS);}one.dwSize=two.dwSize=sizeof(one);
 v.device=&device;device.render_target=&target;one.dwWidth=640;one.dwHeight=480;one.dvScaleX=320;one.dvScaleY=240;one.dvMaxX=one.dvMaxY=1;one.dvMinZ=.5f;one.dvMaxZ=2;
 MUST(d3d_viewport_SetViewport(&v,&one)==D3D_OK);MUST(d3d_viewport_GetViewport(&v,&saved)==DDERR_INVALIDPARAMS);saved.dwSize=sizeof(saved);MUST(d3d_viewport_GetViewport(&v,&saved)==D3D_OK&&saved.dvMinZ==0&&saved.dvMaxZ==1);MUST(d3d_viewport_GetViewport2(&v,&two)==D3D_OK&&two.dvClipX==-1&&two.dvClipY==1&&two.dvClipWidth==2&&two.dvClipHeight==2&&two.dvMinZ==0&&two.dvMaxZ==1);
 const float values[][10]={{2,-1.75f,2.5f,-1.5f,.5f,2,192,-240,4.5f,-1.75f},{-1.5f,1.75f,-1.5f,2,2,.5f,-320,180,-3,1.75f}};
 for(unsigned i=0;i<2;i++){two=(D3DVIEWPORT2){.dwSize=sizeof(two),.dwX=160,.dwY=120,.dwWidth=480,.dwHeight=360,.dvClipX=values[i][0],.dvClipY=values[i][1],.dvClipWidth=values[i][2],.dvClipHeight=values[i][3],.dvMinZ=values[i][4],.dvMaxZ=values[i][5]};MUST(d3d_viewport_SetViewport2(&v,&two)==D3D_OK);one.dwSize=sizeof(one);MUST(d3d_viewport_GetViewport(&v,&one)==D3D_OK&&one.dvScaleX==values[i][6]&&one.dvScaleY==values[i][7]&&one.dvMaxX==values[i][8]&&one.dvMaxY==values[i][9]&&one.dvMinZ==0&&one.dvMaxZ==1);D3DVIEWPORT2 copy={.dwSize=sizeof(copy)};MUST(d3d_viewport_GetViewport2(&v,&copy)==D3D_OK&&!memcmp(&copy,&two,sizeof(copy)));}
 one.dvScaleX=192;one.dvScaleY=-240;one.dvMaxX=4.5f;one.dvMaxY=-1.75f;one.dvMinZ=2;one.dvMaxZ=.5f;MUST(d3d_viewport_SetViewport(&v,&one)==D3D_OK);two.dwSize=sizeof(two);MUST(d3d_viewport_GetViewport2(&v,&two)==D3D_OK&&two.dvClipX==-1.25f&&two.dvClipY==-.75f&&two.dvClipWidth==2.5f&&two.dvClipHeight==-1.5f&&two.dvMinZ==0&&two.dvMaxZ==1);
 device.legacy_current_viewport=&v;unsigned prior=applies;MUST(d3d_viewport_SetViewport(&v,&one)==D3D_OK&&applies==prior+1);device.legacy_current_viewport=NULL;
 const DWORD offsets[]={0,1,320,640,641,0xffffffff},widths[]={0,1,320,640,641,0xffffffff};for(unsigned version=1;version<=3;version++){device.creation_version=version;for(unsigned x=0;x<6;x++)for(unsigned w=0;w<6;w++){one.dwX=offsets[x];one.dwY=0;one.dwWidth=widths[w];one.dwHeight=480;D3DLegacyViewport before=v;BOOL valid=version==1||(offsets[x]<=640&&widths[w]<=640-offsets[x]);MUST(d3d_viewport_SetViewport(&v,&one)==(valid?D3D_OK:DDERR_INVALIDPARAMS));if(!valid)MUST(!memcmp(&before,&v,sizeof(v)));}}
 v.device=NULL;MUST(d3d_viewport_SetViewport(&v,&one)==D3DERR_VIEWPORTHASNODEVICE);
}
int main(void){for(unsigned i=0;i<64;i++){owner_cases();contracts();}printf("PASS actual legacy viewport C: %u checks, exact descriptor sizes, private parent lifetime, unbound state, both coordinate conversions, native depth ranges, device-version bounds and failed-call atomicity\\n",checks);return 0;}
`;
try{const file=directory+"/viewport.c",binary=directory+"/viewport";fs.writeFileSync(file,code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(directory,{recursive:true,force:true});}
