"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_LEGACY_HANDLES_SOURCE||root+"/ddrawproxy/d3d_legacy_proxy.inc","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-handles-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end);}
const code=`#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t UINT,DWORD,ULONG,D3DMATERIALHANDLE,D3DTEXTUREHANDLE,D3DMATRIXHANDLE;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef unsigned char BYTE;
typedef void IDirect3DMaterial3,IDirect3DDevice3,IDirect3DDevice7,IDirect3DDevice,IDirectDraw7;
typedef struct{DWORD dwSize,color;}D3DMATERIAL;typedef struct{DWORD color;}D3DMATERIAL7;
typedef struct{float words[16];}D3DMATRIX;typedef struct D3DLegacyMatrix{DWORD handle;D3DMATRIX matrix;struct D3DLegacyMatrix*next;}D3DLegacyMatrix;
typedef struct D3D7Device D3D7Device;typedef struct D3DLegacyMaterial D3DLegacyMaterial;typedef struct DDSurface DDSurface;
typedef struct DDrawObject{const int*vtbl7;LONG ref,lifetime_ref;D3DLegacyMaterial*legacy_materials;DDSurface*surfaces;D3D7Device*d3d7_device;struct DDrawObject*next;}DDrawObject;
struct DDSurface{DDrawObject*owner;DWORD texture_handle,format;DDSurface*next;};
struct D3D7Device{const int*device3_vtbl,*device1_vtbl;DDrawObject*owner;D3D7Device*next_in_owner;unsigned creation_version;DWORD render_states[256];DDSurface*bound;D3DLegacyMaterial*legacy_material;D3DMATERIAL7 material;D3DLegacyMatrix*legacy_matrices;D3DMATRIX transforms[24],legacy_projection;D3DMATRIXHANDLE legacy_transform_handles[24];};
${source.match(/typedef struct D3DLegacyMaterialHandle \{[\s\S]*?\} D3DLegacyMaterialHandle;/)[0]}
struct D3DLegacyMaterial{const int*vtbl3;LONG ref;DDrawObject*owner;D3DMATERIAL material;D3DLegacyMaterialHandle*handles;D3DLegacyMaterial*next;};
#define FALSE 0
#define TRUE 1
#define D3D7_MAX_TRANSFORMS 24u
#define D3DTRANSFORMSTATE_PROJECTION 3
#define D3DERR_INVALIDMATRIX (-8)
typedef unsigned D3DTRANSFORMSTATETYPE;
#define SUCCEEDED(h) ((h)>=0)
#define WINAPI
#define DDWG_TRACE_ERROR(h) (h)
#define D3D_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_OUTOFMEMORY (-2)
#define E_INVALIDARG (-3)
#define D3DERR_INITFAILED (-4)
#define D3DERR_MATRIX_SETDATA_FAILED (-5)
#define D3DERR_MATRIX_GETDATA_FAILED (-6)
#define D3DERR_MATRIX_DESTROY_FAILED (-7)
#define D3DRENDERSTATE_TEXTUREHANDLE 1
#define FAILED(h) ((h)<0)
static unsigned checks,allocs,fail_alloc,rebinds,next_handle=1;static DDrawObject*g_objects;static D3D7Device*fail_bind,*fail_material;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"legacy handles line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static void*heap_alloc_zero(size_t n){if(fail_alloc)return NULL;allocs++;return calloc(1,n);}static void heap_free(void*p){MUST(p&&allocs);allocs--;free(p);}
static DWORD allocate_handle(void){return next_handle++;}static LONG InterlockedDecrement(LONG*p){return --*p;}
static D3DLegacyMaterial*d3d_material_from_any(void*p){return p;}
static void ddraw_child_detach(DDrawObject*d,BOOL public_ref){MUST(!public_ref&&d->ref==1&&d->lifetime_ref==1);--d->lifetime_ref;}
static BOOL format_is_depth(DWORD f){return f==75;}
static void d3d_legacy_material7(const D3DMATERIAL*s,D3DMATERIAL7*d){d->color=s->color;}
static HRESULT d3d7_device_SetMaterial(IDirect3DDevice7*p,D3DMATERIAL7*value){D3D7Device*d=p;if(d==fail_material)return D3DERR_INITFAILED;d->material=*value;return D3D_OK;}
static D3D7Device*d3d1_device_from_iface(IDirect3DDevice*p){return (void*)((BYTE*)p-offseTof(D3D7Device,device1_vtbl));}
static void d3d7_identity_matrix(D3DMATRIX*m){memset(m,0,sizeof(*m));for(unsigned i=0;i<4;i++)m->words[i*5]=1;}
static unsigned transform_updates;static BOOL fail_transform;
static HRESULT d3d_legacy_set_transform(D3D7Device*d,D3DTRANSFORMSTATETYPE state,D3DMATRIX*m){if(!state||state>=24)return DDERR_INVALIDPARAMS;if(fail_transform)return D3DERR_INITFAILED;transform_updates++;d->transforms[state]=*m;if(state==3)d->legacy_projection=*m;return D3D_OK;}
static void d3d7_multiply_matrix(const D3DMATRIX*a,const D3DMATRIX*b,D3DMATRIX*out){D3DMATRIX r={0};for(unsigned i=0;i<4;i++)for(unsigned j=0;j<4;j++)for(unsigned k=0;k<4;k++)r.words[i*4+j]+=a->words[i*4+k]*b->words[k*4+j];*out=r;}
${["d3d_legacy_matrix_link","d3d_legacy_find_matrix","d3d_execute_bind_transform","d3d_execute_refresh_transforms","d3d_execute_update_matrix","d3d1_device_CreateMatrix","d3d1_device_SetMatrix","d3d1_device_GetMatrix","d3d1_device_DeleteMatrix","d3d_material_SetMaterial","d3d_legacy_find_material","d3d_material_get_handle","d3d_legacy_forget_material_handles","d3d_material_Release","d3d_texture_get_handle","d3d_legacy_find_texture"].map(extract).join("\n")}
static HRESULT d3d3_device_SetRenderState(IDirect3DDevice3*p,DWORD state,DWORD value){D3D7Device*d=(void*)((BYTE*)p-offseTof(D3D7Device,device3_vtbl));MUST(state==D3DRENDERSTATE_TEXTUREHANDLE&&d->creation_version<=3&&d->render_states[state]==value);rebinds++;if(d==fail_bind)return D3DERR_INITFAILED;d->bound=d3d_legacy_find_texture(d->owner,value);MUST(d->bound);return D3D_OK;}
${extract("d3d_legacy_swap_texture_handles")}
static void bindings(D3D7Device*owner,D3D7Device*consumer){
 DWORD h[3];D3DMATRIX identity,got;d3d7_identity_matrix(&identity);for(unsigned i=0;i<3;++i)MUST(!d3d1_device_CreateMatrix(&owner->device1_vtbl,&h[i]));
 D3DMATRIX left=identity,scale=identity;left.words[12]=-.75f;scale.words[0]=.5f;MUST(!d3d1_device_SetMatrix(&owner->device1_vtbl,h[1],&left)&&!d3d1_device_SetMatrix(&owner->device1_vtbl,h[2],&scale));
 for(unsigned state=1;state<=3;++state)MUST(!d3d_execute_bind_transform(consumer,state,h[0])&&consumer->legacy_transform_handles[state]==h[0]);
 const float expected_x[]={0,.75f,0,-.75f,-.375f},expected_scale[]={1,1,1,1,.5f};
 for(unsigned phase=0;phase<5;++phase){if(phase==1||phase==2){D3DMATRIX m=identity;m.words[12]=phase==1?.75f:0;MUST(!d3d1_device_SetMatrix(&owner->device1_vtbl,h[0],&m));}if(phase==3)MUST(!d3d_execute_update_matrix(owner,h[0],h[1],0,FALSE));if(phase==4)MUST(!d3d_execute_update_matrix(owner,h[0],h[1],h[2],TRUE));MUST(!d3d_execute_refresh_transforms(consumer));MUST(!d3d1_device_GetMatrix(&owner->device1_vtbl,h[0],&got));MUST(got.words[0]==expected_scale[phase]&&got.words[12]==expected_x[phase]);for(unsigned state=1;state<=3;++state)MUST(!memcmp(&consumer->transforms[state],&got,64));MUST(!memcmp(&consumer->legacy_projection,&got,64));unsigned sent=transform_updates;MUST(!d3d_execute_refresh_transforms(consumer)&&transform_updates==sent);}
 D3DMATRIX old=consumer->transforms[1],changed=identity;changed.words[12]=.25f;MUST(!d3d1_device_SetMatrix(&owner->device1_vtbl,h[0],&changed));fail_transform=1;MUST(d3d_execute_refresh_transforms(consumer)==D3DERR_INITFAILED&&!memcmp(&old,&consumer->transforms[1],64));fail_transform=0;MUST(!d3d_execute_refresh_transforms(consumer)&&consumer->transforms[1].words[12]==.25f);
 MUST(d3d_execute_update_matrix(owner,h[0],h[1],0,TRUE)==D3DERR_INVALIDMATRIX);
 MUST(d3d_execute_bind_transform(consumer,1,0)==D3DERR_INVALIDMATRIX);MUST(d3d_execute_bind_transform(consumer,24,h[0])==DDERR_INVALIDPARAMS);MUST(d3d_execute_update_matrix(owner,0,h[1],0,FALSE)==D3DERR_INVALIDMATRIX);MUST(d3d_execute_update_matrix(owner,h[0],0,0,FALSE)==D3DERR_INVALIDMATRIX);MUST(d3d_execute_update_matrix(owner,h[0],h[1],0xffffffffu,TRUE)==D3DERR_INVALIDMATRIX);
 for(unsigned i=0;i<3;++i)MUST(!d3d1_device_DeleteMatrix(&owner->device1_vtbl,h[i]));memset(consumer->legacy_transform_handles,0,sizeof(consumer->legacy_transform_handles));
}
static void cases(void){DDrawObject a={.ref=1,.lifetime_ref=1},b={.ref=1};a.next=&b;g_objects=&a;D3D7Device d1={.owner=&a,.creation_version=1},d2={.owner=&a,.creation_version=2},d3={.owner=&b,.creation_version=3},d7={.owner=&b,.creation_version=7};a.d3d7_device=&d2;d2.next_in_owner=&d1;b.d3d7_device=&d7;d7.next_in_owner=&d3;
 D3DLegacyMaterial*m=heap_alloc_zero(sizeof(*m));m->ref=1;m->owner=&a;a.legacy_materials=m;d1.legacy_material=m;d3.legacy_material=m;m->ref+=2;D3DMATERIAL data={sizeof(data),0x102030},original=data;MUST(d3d_material_SetMaterial(m,&data)==D3D_OK&&d1.material.color==data.color&&d3.material.color==data.color&&!d2.material.color&&!memcmp(&data,&original,sizeof(data)));data.color=0x405060;fail_material=&d1;MUST(d3d_material_SetMaterial(m,&data)==D3DERR_INITFAILED&&m->material.color==data.color&&d1.material.color==0x102030&&d3.material.color==data.color);fail_material=NULL;MUST(d3d_material_SetMaterial(m,&data)==D3D_OK&&d1.material.color==data.color);original=data;data.dwSize=0;MUST(d3d_material_SetMaterial(m,&data)==DDERR_INVALIDPARAMS&&m->material.color==original.color);MUST(d3d_material_SetMaterial(m,NULL)==DDERR_INVALIDPARAMS);d1.legacy_material=NULL;d3.legacy_material=NULL;MUST(d3d_material_Release(m)==2&&d3d_material_Release(m)==1);DWORD h1=0,h2=0,h3=0,h4=0;MUST(d3d_material_get_handle(m,&d1,&h1)==D3D_OK&&h1);MUST(d3d_material_get_handle(m,&d1,&h2)==D3D_OK&&h1==h2);MUST(d3d_material_get_handle(m,&d2,&h2)==D3D_OK&&h1!=h2);MUST(d3d_material_get_handle(m,&d3,&h3)==D3D_OK&&h3!=h1&&h3!=h2);MUST(d3d_legacy_find_material(&b,h1)==m&&d3d_legacy_find_material(&a,h3)==m&&!d3d_legacy_find_material(&b,0));unsigned old=allocs;h4=0xdeadbeef;fail_alloc=1;MUST(d3d_material_get_handle(m,&d7,&h4)==DDERR_OUTOFMEMORY&&h4==0xdeadbeef&&allocs==old);fail_alloc=0;MUST(d3d_material_get_handle(m,NULL,&h4)==DDERR_INVALIDPARAMS&&h4==0xdeadbeef);MUST(d3d_material_get_handle(m,&d1,NULL)==DDERR_INVALIDPARAMS);d3d_legacy_forget_material_handles(&d2);MUST(!d3d_legacy_find_material(&a,h2)&&d3d_legacy_find_material(&b,h1)==m&&allocs==old-1);MUST(d3d_material_get_handle(m,&d2,&h4)==D3D_OK&&h4!=h2);d3d_legacy_forget_material_handles(&d1);MUST(!d3d_legacy_find_material(&b,h1)&&d3d_legacy_find_material(&a,h3)==m);MUST(!d3d_material_Release(m)&&!a.legacy_materials&&a.ref==1&&!a.lifetime_ref&&!allocs&&!d3d_legacy_find_material(&b,h3));
 DDSurface first={.owner=&a,.format=22},second={.owner=&b,.format=21},depth={.owner=&a,.format=75};a.surfaces=&first;b.surfaces=&second;MUST(!d3d_legacy_find_texture(&a,0));MUST(d3d_legacy_swap_texture_handles(&first,&second)==E_INVALIDARG&&!first.texture_handle&&!second.texture_handle);MUST(d3d_texture_get_handle(&first,&d1,&h1)==D3D_OK&&h1);MUST(d3d_legacy_swap_texture_handles(&first,&second)==E_INVALIDARG&&first.texture_handle==h1&&!second.texture_handle);MUST(d3d_texture_get_handle(&first,&d2,&h2)==D3D_OK&&h1==h2);MUST(d3d_texture_get_handle(&first,&d3,&h2)==D3D_OK&&h1==h2);MUST(d3d_texture_get_handle(&second,&d2,&h2)==D3D_OK&&h1!=h2);h3=0xdeadbeef;MUST(d3d_texture_get_handle(&depth,&d1,&h3)==DDERR_INVALIDPARAMS&&h3==0xdeadbeef);MUST(d3d_texture_get_handle(&first,NULL,&h3)==DDERR_INVALIDPARAMS);MUST(d3d_texture_get_handle(&first,&d1,NULL)==DDERR_INVALIDPARAMS);
 d1.render_states[1]=h1;d2.render_states[1]=h2;d3.render_states[1]=h1;d7.render_states[1]=h1;MUST(d3d_legacy_swap_texture_handles(&first,&second)==D3D_OK&&first.texture_handle==h2&&second.texture_handle==h1);MUST(d3d_legacy_find_texture(&a,h1)==&second&&d3d_legacy_find_texture(&b,h2)==&first);MUST(d1.bound==&second&&d2.bound==&first&&d3.bound==&second&&!d7.bound&&d1.render_states[1]==h1&&d2.render_states[1]==h2);MUST(d3d_legacy_swap_texture_handles(&first,&second)==D3D_OK&&d1.bound==&first&&d2.bound==&second);fail_bind=&d2;MUST(d3d_legacy_swap_texture_handles(&first,&second)==D3DERR_INITFAILED&&first.texture_handle==h2&&second.texture_handle==h1);fail_bind=NULL;MUST(d3d_legacy_swap_texture_handles(&first,&second)==D3D_OK&&d1.bound==&first&&d2.bound==&second&&d3.bound==&first);MUST(d3d_legacy_swap_texture_handles(&first,&first)==D3D_OK&&first.texture_handle==h1&&d1.bound==&first&&!d7.bound);
 bindings(&d1,&d3);
 DWORD matrix_handle=0;D3DMATRIX matrix,expected;MUST(d3d1_device_CreateMatrix(&d1.device1_vtbl,&matrix_handle)==D3D_OK&&matrix_handle);d3d7_identity_matrix(&expected);MUST(d3d1_device_GetMatrix(&d3.device1_vtbl,matrix_handle,&matrix)==D3D_OK&&!memcmp(&matrix,&expected,sizeof(matrix)));expected.words[5]=2;expected.words[10]=3;expected.words[15]=4;MUST(d3d1_device_SetMatrix(&d3.device1_vtbl,matrix_handle,&expected)==D3D_OK);MUST(d3d1_device_GetMatrix(&d1.device1_vtbl,matrix_handle,&matrix)==D3D_OK&&!memcmp(&matrix,&expected,sizeof(matrix)));MUST(d3d1_device_DeleteMatrix(&d2.device1_vtbl,matrix_handle)==D3D_OK&&!d1.legacy_matrices&&!allocs);memset(&matrix,0xa5,sizeof(matrix));D3DMATRIX untouched=matrix;MUST(d3d1_device_GetMatrix(&d3.device1_vtbl,matrix_handle,&matrix)==D3DERR_MATRIX_GETDATA_FAILED&&!memcmp(&matrix,&untouched,sizeof(matrix)));MUST(d3d1_device_DeleteMatrix(&d1.device1_vtbl,matrix_handle)==D3DERR_MATRIX_DESTROY_FAILED);MUST(d3d1_device_SetMatrix(&d1.device1_vtbl,0,NULL)==D3DERR_MATRIX_SETDATA_FAILED);fail_alloc=1;matrix_handle=0xdeadbeef;MUST(d3d1_device_CreateMatrix(&d2.device1_vtbl,&matrix_handle)==DDERR_OUTOFMEMORY&&!matrix_handle&&!allocs);fail_alloc=0;
}
int main(void){for(unsigned i=0;i<64;i++)cases();MUST(!allocs&&rebinds);printf("PASS actual legacy handle C: %u checks, shared-material updates across owners, stable per-device material aliases, global texture/matrix identities, foreign-owner lookup, uninitialized swap rejection, rebinding all legacy contexts, retained handle values, failed binding prefixes/retry, alias OOM and destruction cleanup\\n",checks);return 0;}
`.replaceAll("offseTof","offsetof");
try{const file=dir+"/handles.c",binary=dir+"/handles";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
