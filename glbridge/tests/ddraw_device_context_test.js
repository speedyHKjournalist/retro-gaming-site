"use strict";
// Native contracts are pinned by Wine ddraw2/4/7 test_device_qi and
// test_viewport_object. Compile the actual entry points with sanitizers.
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-context-"));
const legacy=fs.readFileSync(process.env.DDRAW_CONTEXT_LEGACY_SOURCE||root+"/ddrawproxy/d3d_legacy_proxy.inc","utf8"),modern=fs.readFileSync(process.env.DDRAW_CONTEXT_DEVICE_SOURCE||root+"/ddrawproxy/d3d7_proxy.inc","utf8");
function extract(name,input=legacy){const match=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(input);assert(match,name);let end=input.indexOf("{",match.index)+1,depth=1;for(;depth;end++){if(input[end]==="{")depth++;if(input[end]==="}")depth--;}return input.slice(match.index,end);}
const code=`#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
typedef unsigned char BYTE;typedef uint32_t DWORD,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;
typedef struct{unsigned value;}GUID;typedef const GUID*REFIID;
static const GUID IID_IUnknown={0},IID_IDirect3DDevice={1},IID_IDirect3DDevice2={2},IID_IDirect3DDevice3={3},IID_IDirect3DDevice7={7},unsupported={50};
typedef void IDirect3DDevice,IDirect3DDevice2,IDirect3DDevice3,IDirect3DDevice7,IDirect3DViewport2,IDirect3DViewport3,IDirect3DMaterial3;
typedef struct D3DLegacyViewport D3DLegacyViewport;
typedef struct D3DLegacyMatrix{struct D3DLegacyMatrix*next;}D3DLegacyMatrix;
typedef struct{void*vtbl3;}Material;
typedef struct{const int*vtbl,*device3_vtbl,*device2_vtbl,*device1_vtbl;LONG ref;unsigned creation_version;void*owner;D3DLegacyViewport*legacy_current_viewport,*legacy_viewports;D3DLegacyMatrix*legacy_matrices;Material*legacy_material;void*legacy_vertices,*legacy_indices,*legacy_pick_records;}D3D7Device;
struct D3DLegacyViewport{const int*vtbl3,*vtbl2;LONG ref;void*owner;D3D7Device*device;D3DLegacyViewport*next_in_device;BOOL viewport1_set,viewport2_set;};
#define WINAPI
#define S_OK 0
#define D3D_OK 0
#define E_POINTER (-1)
#define E_NOINTERFACE (-2)
#define DDERR_INVALIDPARAMS (-3)
#define D3DERR_VIEWPORTHASNODEVICE (-4)
#define D3DERR_NOCURRENTVIEWPORT (-5)
#define DDWG_TRACE_ERROR(h) (h)
#define FAILED(h) ((h)<0)
static unsigned checks,applies,fail_apply;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"context line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static BOOL guid_equal(REFIID a,REFIID b){return a&&b&&a->value==b->value;}static BOOL iid_is_unknown(REFIID a){return guid_equal(a,&IID_IUnknown);}
${extract("d3d7_device_from_iface",modern)}
${extract("d3d3_device_from_iface")}
${extract("d3d2_device_from_iface")}
static ULONG d3d7_device_AddRef(IDirect3DDevice7*p){return ++((D3D7Device*)p)->ref;}
#define IDirect3DDevice7_AddRef(p) d3d7_device_AddRef(p)
static ULONG d3d_viewport_AddRef(IDirect3DViewport3*p){return ++((D3DLegacyViewport*)p)->ref;}
static ULONG d3d_viewport_Release(IDirect3DViewport3*p){D3DLegacyViewport*v=p;MUST(v->ref>0);return --v->ref;}
static HRESULT d3d_legacy_apply_viewport(D3DLegacyViewport*v){MUST(v->device);applies++;return fail_apply?-90:D3D_OK;}
static void heap_free(void*p){MUST(!p);}
static ULONG d3d_material_Release(IDirect3DMaterial3*p){(void)p;MUST(0);return 0;}
static void d3d_legacy_forget_material_handles(D3D7Device*d){(void)d;}
${extract("d3d7_device_QueryInterface",modern)}
${["d3d3_device_QueryInterface","d3d2_device_QueryInterface","d3d_legacy_add_viewport","d3d_legacy_delete_viewport","d3d_legacy_set_current_viewport","d3d2_device_GetCurrentViewport","d3d3_device_GetCurrentViewport","d3d_legacy_device_cleanup"].map(n=>extract(n)).join("\n")}
static HRESULT qi(D3D7Device*d,unsigned view,REFIID iid,void**out){if(view==3)return d3d3_device_QueryInterface(&d->device3_vtbl,iid,out);if(view==2)return d3d2_device_QueryInterface(&d->device2_vtbl,iid,out);return d3d7_device_QueryInterface(d,iid,out);}
static void matrix(unsigned version){D3D7Device d={.ref=1,.creation_version=version};const GUID*ids[]={&IID_IUnknown,&IID_IDirect3DDevice,&IID_IDirect3DDevice2,&IID_IDirect3DDevice3,&IID_IDirect3DDevice7,&unsupported,NULL};void*views[]={version==7?(void*)&d.vtbl:version==3?(void*)&d.device3_vtbl:&d.device2_vtbl,&d.device1_vtbl,&d.device2_vtbl,&d.device3_vtbl,&d.vtbl,NULL,NULL};for(unsigned i=0;i<7;i++){BOOL supported=i==0||(version==7?i==4:i>=1&&i<=version);void*out=(void*)1;MUST(qi(&d,version,ids[i],&out)==(supported?S_OK:E_NOINTERFACE));MUST(out==(supported?views[i]:NULL));MUST(d.ref==(supported?2:1));if(supported){for(unsigned j=0;j<7;j++){void*next=(void*)1;BOOL yes=j==0||(version==7?j==4:j>=1&&j<=version);unsigned view=i==1?version:i==0?version:i==4?7:i;/* Device1 thunk shares the same nonaggregated device QI. */if(view==1)view=version;MUST(qi(&d,view,ids[j],&next)==(yes?S_OK:E_NOINTERFACE));MUST(next==(yes?views[j]:NULL));if(yes)MUST(--d.ref==2);}MUST(--d.ref==1);}}MUST(qi(&d,version,&IID_IUnknown,NULL)==E_POINTER&&d.ref==1);}
static void viewports(unsigned version){int owner;D3D7Device d={.creation_version=version,.owner=&owner};D3DLegacyViewport a={.ref=1,.owner=&owner},b={.ref=1,.owner=&owner},foreign={.ref=1};void*out=(void*)0xbaadc0de;MUST(d3d2_device_GetCurrentViewport(&d.device2_vtbl,NULL)==D3DERR_NOCURRENTVIEWPORT);MUST(d3d3_device_GetCurrentViewport(&d.device3_vtbl,(void*)&out)==D3DERR_NOCURRENTVIEWPORT&&out==(void*)0xbaadc0de);MUST(d3d_legacy_set_current_viewport(&d,&a)==DDERR_INVALIDPARAMS);MUST(d3d_legacy_add_viewport(&d,&foreign)==DDERR_INVALIDPARAMS);MUST(d3d_legacy_add_viewport(&d,&a)==D3D_OK&&a.ref==2);MUST(d3d_legacy_add_viewport(&d,&b)==D3D_OK&&b.ref==2);unsigned prior=applies;MUST(d3d_legacy_set_current_viewport(&d,&a)==D3D_OK&&a.ref==3&&applies==prior);MUST(d3d_legacy_set_current_viewport(&d,&a)==D3D_OK&&a.ref==3);MUST(d3d2_device_GetCurrentViewport(&d.device2_vtbl,(void*)&out)==D3D_OK&&out==&a.vtbl2&&a.ref==4);MUST(d3d_viewport_Release(&a)==3);MUST(d3d3_device_GetCurrentViewport(&d.device3_vtbl,(void*)&out)==D3D_OK&&out==&a.vtbl3&&a.ref==4);MUST(d3d_viewport_Release(&a)==3);MUST(d3d_legacy_set_current_viewport(&d,NULL)==DDERR_INVALIDPARAMS&&d.legacy_current_viewport==&a&&a.ref==3);b.viewport2_set=1;fail_apply=1;MUST(d3d_legacy_set_current_viewport(&d,&b)==-90&&d.legacy_current_viewport==&a&&a.ref==3&&b.ref==2);fail_apply=0;MUST(d3d_legacy_set_current_viewport(&d,&b)==D3D_OK&&a.ref==2&&b.ref==3);MUST(d3d_legacy_delete_viewport(&d,&b)==D3D_OK&&b.ref==(version==2?2:1)&&!d.legacy_current_viewport&&!b.device);out=(void*)1;MUST(d3d2_device_GetCurrentViewport(&d.device2_vtbl,(void*)&out)==D3DERR_NOCURRENTVIEWPORT&&out==(void*)1);MUST(d3d_legacy_set_current_viewport(&d,&a)==D3D_OK&&a.ref==3);d3d_legacy_device_cleanup(&d);MUST(!a.device&&a.ref==(version==2?2:1)&&!d.legacy_current_viewport&&!d.legacy_viewports);/* Drop the native Device2-retained references explicitly in this harness. */if(version==2){MUST(d3d_viewport_Release(&a)==1);MUST(d3d_viewport_Release(&b)==1);}MUST(!d3d_viewport_Release(&a)&&!d3d_viewport_Release(&b));}
int main(void){for(unsigned n=0;n<64;n++){matrix(2);matrix(3);matrix(7);viewports(2);viewports(3);}printf("PASS actual legacy device context C: %u checks, creation-version QI matrices/canonical IUnknown, nested queries, unset viewport output, current references, Device2/3 detachment and destruction, failed-apply atomicity\\n",checks);return 0;}
`;
try{const file=dir+"/context.c",binary=dir+"/context";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
