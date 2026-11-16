"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-identity-"));
const current=fs.readFileSync(root+"/ddrawproxy/ddraw_proxy.c","utf8"),legacy=fs.readFileSync(process.env.DDRAW_IDENTITY_LEGACY_SOURCE||root+"/ddrawproxy/d3d_legacy_proxy.inc","utf8");
const source=fs.readFileSync(process.env.DDRAW_IDENTITY_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8");
function extract(name,input=source){const re=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{");let match=re.exec(input);if(!match){input=current;match=re.exec(input);}assert(match,name);let end=input.indexOf("{",match.index)+1,depth=1;for(;depth;end++){if(input[end]==="{")depth++;if(input[end]==="}")depth--;}return input.slice(match.index,end);}
const identifiers=["IDirectDrawSurface","IDirectDrawSurface2","IDirectDrawSurface3","IDirectDrawSurface4","IDirectDrawSurface7","IDirect3DTexture","IDirect3DTexture2","IDirectDrawGammaControl","IDirectDrawColorControl","IDirect3DHALDevice","IDirect3DRGBDevice","IDirect3DRampDevice","IDirect3DMMXDevice","IDirect3DRefDevice","IDirect3DDevice","IDirect3DDevice2","IDirect3DDevice3","IDirect3DDevice7"];
const code=`#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int32_t LONG,HRESULT;typedef uint32_t ULONG,UINT,DWORD;typedef unsigned char BYTE;typedef int BOOL;typedef size_t SIZE_T;
typedef struct {unsigned Data1;} GUID;typedef const GUID*REFIID;static const GUID IID_IUnknown={0};
${identifiers.map((name,i)=>"static const GUID IID_"+name+"={"+(i+1)+"};").join("\n")}
typedef struct {const int*lpVtbl;} IUnknown;
typedef IUnknown IDirectDraw7,IDirectDrawSurface7,IDirectDrawGammaControl,IDirect3DDevice,IDirect3DDevice3,IDirect3DDevice7,IDirect3DTexture,IDirect3DTexture2;
typedef struct DDSurface DDSurface;typedef struct D3D7Device D3D7Device;
typedef struct DDrawObject{void*vtbl7;LONG ref,lifetime_ref;D3D7Device*d3d7_device;}DDrawObject;
struct DDSurface{const int*vtbl7,*vtbl4,*vtbl3,*vtbl2,*vtbl1,*texture2_vtbl,*texture1_vtbl,*gamma_vtbl;LONG ref,public_refs[8];unsigned creation_version;D3D7Device*device1;DDrawObject*owner;BOOL implicit;DWORD handle;struct{struct{DWORD dwCaps;}ddsCaps;}desc;};
struct D3D7Device{const int*vtbl,*device3_vtbl,*device2_vtbl,*device1_vtbl;LONG ref;DDrawObject*owner;DDSurface*render_target,*aggregate_surface;unsigned creation_version;};
static const int g_surface_vtbl=7,g_surface_vtbl4=4,g_surface_vtbl3=3,g_surface_vtbl2=2,g_surface_vtbl1=1,g_gamma_vtbl=8,g_d3d_texture1_vtbl=9,g_d3d_texture2_vtbl=10,g_d3d1_device_vtbl=11;
#define DD_VERSION_1 1u
#define DD_VERSION_2 2u
#define DD_VERSION_3 3u
#define DD_VERSION_4 4u
#define DD_VERSION_7 7u
#define WINAPI
#define TRUE 1
#define FALSE 0
#define DD_OK 0
#define D3D_OK 0
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_NOINTERFACE (-2)
#define DDSCAPS_PRIMARYSURFACE 1u
#define DDERR_INVALIDPARAMS E_INVALIDARG
#define D3DERR_INITFAILED (-3)
#define FAILED(h) ((h)<0)
#define DDWG_TRACE_CHECKPOINT(...) ((void)0)
#define DDWG_TRACE(...) ((void)0)
#define DDWG_TRACE_RESULT(h) (h)
#define DDWG_TRACE_ERROR(h) (h)
static unsigned checks,surfaces,devices,created,fail_device;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"identity line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){return --*p;}
static BOOL guid_equal(REFIID a,REFIID b){return a&&b&&a->Data1==b->Data1;}static BOOL iid_is_unknown(REFIID a){return guid_equal(a,&IID_IUnknown);}
static void surface_destroy(DDSurface*);static void d3d_legacy_surface_release_device(DDSurface*);
${["surface_from_any","surface_interface","surface_private_addref","surface_private_release","surface_public_addref","surface_public_release","surface_transfer_public_ref","surface_factory_view","surface_creation_profile","surface_AddRef","surface_Release"].map(n=>extract(n)).join("\n")}
${["d3d1_device_from_iface","d3d_texture1_from_iface","d3d_texture2_from_iface"].map(n=>extract(n,legacy)).join("\n")}
static void ddraw_child_attach(DDrawObject*d,BOOL public_ref){d->lifetime_ref++;if(public_ref){d->ref++;d->lifetime_ref++;}}
static void ddraw_child_detach(DDrawObject*d,BOOL public_ref){if(public_ref){d->ref--;d->lifetime_ref--;}d->lifetime_ref--;MUST(d->lifetime_ref>=d->ref);}
static ULONG ddraw_Release(IDirectDraw7*p){DDrawObject*d=(DDrawObject*)p;d->lifetime_ref--;return --d->ref;}
static ULONG d3d7_device_AddRef(IDirect3DDevice7*p){return ++((D3D7Device*)p)->ref;}
static ULONG d3d7_device_Release(IDirect3DDevice7*p){D3D7Device*d=(D3D7Device*)p;LONG count=--d->ref;MUST(count>=0);if(!count){MUST(!d->render_target&&!d->aggregate_surface);d->owner->d3d7_device=NULL;ddraw_child_detach(d->owner,FALSE);devices--;free(d);}return count;}
static HRESULT d3d3_device_QueryInterface(IDirect3DDevice3*p,REFIID iid,void**out){(void)p;(void)iid;*out=NULL;return E_NOINTERFACE;}
static HRESULT d3d_legacy_create_device(DDrawObject*owner,REFIID iid,DDSurface*s,unsigned version,void**out){(void)iid;if(fail_device||owner->d3d7_device){*out=NULL;return D3DERR_INITFAILED;}D3D7Device*d=calloc(1,sizeof(*d));MUST(d);d->device1_vtbl=&g_d3d1_device_vtbl;d->ref=1;d->owner=owner;d->render_target=s;d->creation_version=version;owner->ref++;owner->lifetime_ref++;owner->d3d7_device=d;surface_AddRef((IDirectDrawSurface7*)&s->vtbl7);*out=&d->device1_vtbl;devices++;created++;return S_OK;}
${extract("d3d_legacy_surface_create_device",legacy)}
${extract("surface_QueryInterface")}
${extract("d3d_legacy_surface_release_device",legacy)}
${["d3d1_device_QueryInterface","d3d1_device_AddRef","d3d1_device_Release","d3d_texture1_QueryInterface","d3d_texture1_AddRef","d3d_texture1_Release","d3d_texture2_QueryInterface","d3d_texture2_AddRef","d3d_texture2_Release"].map(n=>extract(n,legacy)).join("\n")}
${["gamma_from_iface","gamma_QueryInterface","gamma_AddRef","gamma_Release"].map(n=>extract(n)).join("\n")}
static void surface_destroy(DDSurface*s){for(unsigned i=0;i<8;i++)MUST(!s->public_refs[i]);MUST(!s->ref);d3d_legacy_surface_release_device(s);s->owner->lifetime_ref--;surfaces--;free(s);}
static DDSurface*make_surface(DDrawObject*owner,unsigned profile){DDSurface*s=calloc(1,sizeof(*s));MUST(s);s->vtbl7=&g_surface_vtbl;s->vtbl4=&g_surface_vtbl4;s->vtbl3=&g_surface_vtbl3;s->vtbl2=&g_surface_vtbl2;s->vtbl1=&g_surface_vtbl1;s->gamma_vtbl=&g_gamma_vtbl;s->texture1_vtbl=&g_d3d_texture1_vtbl;s->texture2_vtbl=&g_d3d_texture2_vtbl;s->owner=owner;owner->lifetime_ref++;s->ref=s->public_refs[7]=1;s->creation_version=7;surfaces++;surface_creation_profile(s,profile);return s;}
static HRESULT qi(void*p,REFIID iid,void**out){const int*v=*(const int**)p;if(v==&g_d3d1_device_vtbl)return d3d1_device_QueryInterface(p,iid,out);if(v==&g_d3d_texture1_vtbl)return d3d_texture1_QueryInterface(p,iid,out);if(v==&g_d3d_texture2_vtbl)return d3d_texture2_QueryInterface(p,iid,out);if(v==&g_gamma_vtbl)return gamma_QueryInterface(p,iid,out);return surface_QueryInterface(p,iid,out);}
static ULONG add(void*p){const int*v=*(const int**)p;if(v==&g_d3d1_device_vtbl)return d3d1_device_AddRef(p);if(v==&g_d3d_texture1_vtbl)return d3d_texture1_AddRef(p);if(v==&g_d3d_texture2_vtbl)return d3d_texture2_AddRef(p);if(v==&g_gamma_vtbl)return gamma_AddRef(p);unsigned version;DDSurface*s=surface_from_any(p,&version);return surface_public_addref(s,version);}
static ULONG release(void*p){const int*v=*(const int**)p;if(v==&g_d3d1_device_vtbl)return d3d1_device_Release(p);if(v==&g_d3d_texture1_vtbl)return d3d_texture1_Release(p);if(v==&g_d3d_texture2_vtbl)return d3d_texture2_Release(p);if(v==&g_gamma_vtbl)return gamma_Release(p);unsigned version;DDSurface*s=surface_from_any(p,&version);return version==7?surface_Release(p):surface_public_release(s,version);}
struct Row{const GUID*iid;unsigned group;};
static void matrix(void*base,unsigned initial,unsigned profile,BOOL device){struct Row rows[]={{&IID_IUnknown,1},{&IID_IDirectDrawSurface,1},{&IID_IDirectDrawSurface2,2},{&IID_IDirectDrawSurface3,3},{&IID_IDirectDrawSurface4,4},{&IID_IDirectDrawSurface7,7},{&IID_IDirectDrawGammaControl,0},{&IID_IDirect3DTexture,profile},{&IID_IDirect3DTexture2,profile},{&IID_IDirect3DHALDevice,1}};for(unsigned i=0;i<sizeof(rows)/sizeof(*rows);i++){if((profile==7&&i>=7)||(!device&&i==9))continue;void*a=NULL;MUST(qi(base,rows[i].iid,&a)==S_OK&&a);unsigned raised=2+(initial==rows[i].group);MUST(add(a)==raised);MUST(release(a)==raised-1);for(unsigned j=0;j<sizeof(rows)/sizeof(*rows);j++){if((profile==7&&j>=7)||(!device&&j==9))continue;void*b=NULL;MUST(qi(a,rows[j].iid,&b)==S_OK&&b);unsigned expected=(initial==rows[j].group)+(rows[i].group==rows[j].group);MUST(release(b)==expected);}MUST(release(a)==(initial==rows[i].group));}void*unknown=NULL;MUST(qi(base,&IID_IUnknown,&unknown)==S_OK);DDSurface*s=device?d3d1_device_from_iface(base)->aggregate_surface:surface_from_any(base,NULL);MUST(unknown==&s->vtbl1);release(unknown);}
static void ordinary(unsigned profile){DDrawObject owner={.ref=1,.lifetime_ref=1};DDSurface*s=make_surface(&owner,profile);void*base=surface_interface(s,surface_factory_view(profile));matrix(base,surface_factory_view(profile),s->creation_version,FALSE);const GUID*bad[]={&IID_IDirect3DDevice,&IID_IDirect3DDevice2,&IID_IDirect3DDevice3,&IID_IDirect3DDevice7,&IID_IDirect3DRampDevice,&IID_IDirect3DRefDevice};for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){void*out=(void*)1;MUST(qi(base,bad[i],&out)==(profile==7?E_NOINTERFACE:E_INVALIDARG)&&!out);}void*out=(void*)1;MUST(qi(base,NULL,&out)==E_INVALIDARG&&!out);MUST(qi(base,&IID_IDirectDrawColorControl,&out)==E_NOINTERFACE&&!out);if(profile==7){MUST(qi(base,&IID_IDirect3DTexture,&out)==E_NOINTERFACE&&!out);MUST(qi(base,&IID_IDirect3DTexture2,&out)==E_NOINTERFACE&&!out);}MUST(!release(base)&&owner.ref==1&&owner.lifetime_ref==1);}
static void implicit_profile(unsigned profile){DDrawObject owner={.ref=1,.lifetime_ref=1};DDSurface*s=make_surface(&owner,7);s->implicit=TRUE;surface_creation_profile(s,profile);unsigned view=surface_factory_view(profile);void*base=surface_interface(s,view);MUST(s->ref==1&&s->public_refs[view]==1);MUST(add(base)==2&&release(base)==1);MUST(!release(base)&&s->ref==0&&owner.lifetime_ref==2);MUST(add(base)==1&&!release(base));s->implicit=FALSE;surface_destroy(s);MUST(owner.lifetime_ref==1&&owner.ref==1);}
static void aggregation(void){DDrawObject owner={.ref=1,.lifetime_ref=1};DDSurface*s=make_surface(&owner,1);void*device=NULL;fail_device=1;MUST(qi(&s->vtbl1,&IID_IDirect3DHALDevice,&device)==D3DERR_INITFAILED&&!device&&s->ref==1&&!s->device1&&owner.ref==1);fail_device=0;unsigned prior=created;MUST(qi(&s->vtbl1,&IID_IDirect3DHALDevice,&device)==S_OK&&device&&created==prior+1);MUST(release(&s->vtbl1)==1);matrix(device,1,1,TRUE);MUST(owner.ref==1&&owner.lifetime_ref==3&&s->ref==1&&s->public_refs[1]==1&&s->device1->ref==1);void*other=NULL;MUST(qi(device,&IID_IDirectDrawSurface4,&other)==S_OK);MUST(!release(device)&&s->public_refs[1]==0&&s->public_refs[4]==1);void*revived=NULL;MUST(qi(other,&IID_IDirect3DRGBDevice,&revived)==S_OK&&revived==device&&created==prior+1);MUST(add(revived)==2&&release(revived)==1&&!release(revived));MUST(!release(other)&&owner.ref==1&&owner.lifetime_ref==1&&!owner.d3d7_device);}
int main(void){for(unsigned i=0;i<32;i++){ordinary(1);ordinary(2);ordinary(3);ordinary(4);ordinary(7);implicit_profile(1);implicit_profile(2);implicit_profile(3);implicit_profile(4);implicit_profile(7);aggregation();}MUST(!surfaces&&!devices);printf("PASS actual DirectDraw surface identity C: %u checks, independent public views, common IUnknown, legacy texture groups, GammaControl, Device1 aggregation/revival, factory failure atomicity and complete cleanup\\n",checks);return 0;}
`;
try{const file=directory+"/identity.c",binary=directory+"/identity";fs.writeFileSync(file,code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-Wno-unused-const-variable","-fsanitize=address,undefined","-fno-omit-frame-pointer",file,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(directory,{recursive:true,force:true});}
