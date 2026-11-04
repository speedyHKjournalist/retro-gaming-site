'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-implicit-'));
function extract(s,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(s);assert(m,name);let end=s.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(s[end]==='{')depth++;if(s[end]==='}')depth--;}return s.slice(m.index,end);}
const apis=process.env.D9WG_IMPLICIT_API?[Number(process.env.D9WG_IMPLICIT_API)]:[8,9];assert(apis.every(api=>api===8||api===9));
try{for(const api of apis){const source=fs.readFileSync(process.env['D'+api+'WG_IMPLICIT_FRONTEND']||root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8');fs.writeFileSync(dir+'/implicit.c',`#include <stdint.h>
/* No frontend views are attached in this isolated lifetime fixture. */
#define core_view_retire8(object) ((void)(object))
#include "${root}/runtime/d9wg_depth_surface_layout8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_private_data.h"
#include "runtime/d9wg_reset.h"
#define D8WG_MAX_STREAMS 16
#define D8WG_MAX_TEXTURE_STAGES 8
typedef uint32_t UINT,DWORD,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL,D3DFORMAT,D3DPOOL,D3DBACKBUFFER_TYPE,D3DCUBEMAP_FACES;typedef uint8_t BYTE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-2)
#define D3DERR_NOTFOUND (-3)
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define D3DPOOL_DEFAULT 0
#define D3DFMT_UNKNOWN 0
#define D3DMULTISAMPLE_NONE 0
#define D3DBACKBUFFER_TYPE_MONO 0
#define D9_MAX_RENDER_TARGETS 4
#define SUCCEEDED(v) ((v)>=0)
#define HEAP_ZERO_MEMORY 1
#define D8WG_TRACE_ERROR(v) (v)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#define TRACE_FLUSH(...)
#define TRACE_SURFACE_FREED(...)
#define TRACE_REGISTER_RANGE(...)
#define ZeroMemory(p,n) memset(p,0,n)
typedef struct Interface Interface;typedef struct Vtable{ULONG(*AddRef)(Interface*);ULONG(*Release)(Interface*);}Vtable;struct Interface{const Vtable*lpVtbl;};
typedef struct FakeChain{Interface iface;UINT handle;LONG refcount;struct FakeChain*next_device_chain;}FakeChain;typedef FakeChain D9SwapChain;
${api===9?"static void swap_chain_destroy_storage(FakeChain*p){(void)p;assert(0);}":""}
typedef Interface IDirect3DDevice${api},IDirect3DSurface${api};typedef struct Device Device;typedef struct Surface Surface;typedef struct Texture Texture;
typedef struct{unsigned locked;}Level;
struct Texture{Interface iface;LONG refcount,binding_refs;Device*device;Texture*next_device_resource;UINT pool;BOOL locked;UINT level_count;Level*levels;BOOL private_back_buffer;};
typedef struct{UINT Width,Height;D3DFORMAT Format;}Mode;
typedef struct{UINT BackBufferWidth,BackBufferHeight,BackBufferCount,MultiSampleType,MultiSampleQuality,AutoDepthStencilFormat,BackBufferFormat;BOOL EnableAutoDepthStencil;}Parameters;
typedef Parameters D3DPRESENT_PARAMETERS;
typedef struct{Interface iface;}Parent;
typedef struct{D3DFORMAT Format;UINT Type,Usage,Pool,Size,MultiSampleType,Width,Height;}D3DSURFACE_DESC;
#define D3DRTYPE_SURFACE 1
struct Device{Interface iface;LONG refcount,child_parent_refs;BOOL releasing_owned_refs;Parent*parent;UINT handle;Mode display_mode;Parameters present;Surface*implicit_back_buffer,*implicit_depth_stencil,*depth_surface,*depth_stencil_surface,*render_target_surfaces[4];BOOL depth_surface_enabled,depth_stencil_unbound;UINT implicit_surface_count,additional_swap_chain_count;Texture*render_target_texture,*render_target_cube,*vertex_buffers,*index_buffers,*texture_resources,*cube_resources,*volume_resources,*textures[8];struct{Texture*buffer;}streams[16];Texture*index_buffer;UINT render_target_level,render_target_face;BOOL in_scene;void*recording_state_block,*vertex_shaders,*pixel_shaders,*extra_lights,*front_shadow;int palettes;FakeChain implicit_swap_chain;D9WGPrivateStore back_buffer_private_data,depth_surface_private_data;};
struct Surface{Interface iface;LONG refcount,binding_refs;Device*device;Texture*texture,*cube,*cube_texture;FakeChain*swap_chain;void*shadow,*retained_private_data,*dc,*dc_previous,*dib;BOOL auto_depth_stencil,texture_child,reset_blocker,backbuffer,depth_stencil,locked,additional_back_buffer,device_pin_transferred,device_container;UINT level,face,width,height,multisample,multisample_quality,row_pitch;D3DFORMAT format;D3DSURFACE_DESC desc;D9WGPrivateStore private_data;D9WGPrivateStore*private_data_owner;};
typedef Device D${api}Device;typedef Surface D${api}Surface;typedef Texture D${api}Texture,D${api}CubeTexture,D${api}VolumeTexture,D${api}VertexBuffer,D${api}IndexBuffer;
static Device*device_from_iface(Interface*p){return (Device*)p;}static Surface*surface_from_iface(Interface*p){return (Surface*)p;}
static Vtable g_surface_vtbl;static unsigned allocations,live,fail_at,parent_refs=1,destroys;static const D9WGPrivateOps d9wg_private_ops={0};static int g_transport_lock;
static void*GetProcessHeap(void){return NULL;}static void*HeapAlloc(void*h,UINT f,size_t n){(void)h;(void)f;allocations++;if(allocations==fail_at)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void HeapFree(void*h,UINT f,void*p){(void)h;(void)f;if(p){assert(live);live--;free(p);}}
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){return --*p;}static LONG InterlockedCompareExchange(LONG*p,LONG v,LONG c){LONG old=*p;if(old==c)*p=v;return old;}
${api===9?"static void cube_binding_add_ref(D9Texture*t){(void)t;assert(0); }\nstatic void cube_binding_release(D9Texture*t){(void)t;assert(0); }\nstatic void texture_binding_add_ref(D9Texture*t){(void)t;assert(0);}\nstatic void texture_binding_release(D9Texture*t){(void)t;assert(0);}":""}
static void device_child_add_ref(Device*);static void device_child_release(Device*);
#define IDirect3DDevice${api}_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define IDirect3DDevice${api}_Release(p) ((p)->lpVtbl->Release(p))
#define IDirect3DSurface${api}_AddRef(p) ((p)->lpVtbl->AddRef(p))
#define IDirect3DSurface${api}_Release(p) ((p)->lpVtbl->Release(p))
#define IDirect3DTexture${api}_AddRef(p) ((void)(p),0u)
#define IDirect3DCubeTexture${api}_AddRef(p) ((void)(p),0u)
#define IDirect3DTexture${api}_Release(p) ((void)(p),0u)
#define IDirect3DCubeTexture${api}_Release(p) ((void)(p),0u)
static void parent_release(Interface*p){(void)p;assert(parent_refs==1);parent_refs--;}
#define IDirect3D${api}_Release parent_release
static void surface_binding_add_ref(Surface*);static void surface_binding_release(Surface*);
${api===9?'static void surface_destroy_storage(Surface*);':''}
static void device_release_owned_references(Device*d){
${api===8?'Surface*s=d->depth_surface;d->depth_surface=NULL;if(s)surface_binding_release(s);':'for(UINT i=0;i<4;i++){Surface*s=d->render_target_surfaces[i];d->render_target_surfaces[i]=NULL;if(s)surface_binding_release(s);}Surface*s=d->depth_stencil_surface;d->depth_stencil_surface=NULL;if(s)surface_binding_release(s);'}
}
${api===8?`
static BOOL texture_format_layout(D3DFORMAT f,UINT*w,UINT*h,UINT*b){assert(f==22);*w=*h=1;*b=4;return 1;}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){assert(f==22);*p=w*4;*r=h;*b=*p*h;return 1;}
static HRESULT texture_get_surface_level(Interface*t,UINT l,Interface**o){(void)t;(void)l;(void)o;assert(0);return -1;}
static HRESULT cube_get_surface(Interface*t,D3DCUBEMAP_FACES f,UINT l,Interface**o){(void)t;(void)f;(void)l;(void)o;assert(0);return -1;}
static void detach_device_window(Device*d){(void)d;}static void restore_display_mode(Device*d){(void)d;}static void emit_device_destroy_and_flush(UINT h){assert(h==0x9000);destroys++;}static void free_shader_list(void*p){(void)p;}static void free_extra_lights(void**p){(void)p;}
${['free_implicit_surfaces','reset_buffer_owned_refs','reset_texture_owned_refs','device_has_reset_blockers','create_standalone_surface','create_backbuffer_surface'].map(n=>extract(source,n)).join('\n')}`:`
static void SelectObject(void*a,void*b){(void)a;(void)b;}static void DeleteDC(void*p){(void)p;}static void DeleteObject(void*p){(void)p;}
static void d9wg_light_free(void**p){(void)p;}static void d9wg_palette_free(void*p){(void)p;}static void unwatch_window_state(Device*d){(void)d;}static void restore_display_mode(Device*d){(void)d;}
static UINT g_command_count;
static int emit_command(UINT op,const void*p,UINT size){const D9WGDestroyResource*c=p;assert(op==D9WG_OP_DESTROY_RESOURCE&&size==sizeof(*c)&&c->resource_handle==0x9000&&!c->resource_kind);destroys++;return 1;}
static BOOL submit_batch_locked(BOOL p){(void)p;return 1;}`}
${api===9?'static void EnterCriticalSection(int*p){(void)p;} static void LeaveCriticalSection(int*p){(void)p;}':''}
${api===9?`typedef const int*REFIID;
#define E_NOINTERFACE (-4)
static const int IID_IDirect3DSwapChain9=1,IID_IDirect3DResource9=2,IID_IDirect3DBaseTexture9=3,IID_IDirect3DCubeTexture9=4,IID_IDirect3DDevice9=5,IID_IDirect3DTexture9=6,unknown_id=0,bad_iid=99;
static BOOL iid_is_unknown(REFIID p){return p&&!*p;}static BOOL guid_equal(REFIID a,REFIID b){return a&&b&&*a==*b;}
#define IDirect3DSwapChain9_AddRef(p) ((void)(p),0u)
`:''}
${['surface_is_implicit','device_add_ref','device_release','device_child_add_ref','device_child_release','surface_add_ref',...(api===9?['surface_uses_private_bindings','surface_destroy_storage','reset_bound_surface','reset_surface_owned_refs']:[]),'surface_binding_add_ref','surface_binding_release','surface_release',api===8?'device_get_backbuffer':'device_get_back_buffer'].map(n=>extract(source,n)).join('\n')}
${api===9?'#define IDirect3DDevice9_GetBackBuffer device_get_back_buffer':''}
${[api===8?'device_get_depth_surface':'device_get_depth_stencil_surface','device_get_render_target',...(api===9?['surface_get_container']:[])].map(n=>extract(source,n)).join('\n')}
#define BACK(d,t,o) ${api===8?'device_get_backbuffer(&(d)->iface,0,t,o)':'device_get_back_buffer(&(d)->iface,0,0,t,o)'}
#define DEPTH(d,o) ${api===8?'device_get_depth_surface(&(d)->iface,o)':'device_get_depth_stencil_surface(&(d)->iface,o)'}
#define TARGET(d,o) ${api===8?'device_get_render_target(&(d)->iface,o)':'device_get_render_target(&(d)->iface,0,o)'}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Implicit identity API ${api} line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){(void)g_transport_lock;unsigned cases=0;Parent parent={0};Vtable device_vtable={device_add_ref,device_release};g_surface_vtbl=(Vtable){surface_add_ref,surface_release};Device*d=HeapAlloc(NULL,0,sizeof(*d));d->iface.lpVtbl=&device_vtable;d->refcount=1;d->parent=&parent;d->handle=0x9000;d->display_mode=(Mode){64,64,22};d->present=(Parameters){.BackBufferWidth=64,.BackBufferHeight=64,.BackBufferFormat=22,.AutoDepthStencilFormat=75,.EnableAutoDepthStencil=1,.MultiSampleType=4};d->depth_surface_enabled=1;
Interface*out=(void*)(uintptr_t)1;unsigned before=allocations;CHECK(BACK(d,0,NULL)==-1&&DEPTH(d,NULL)==-1&&TARGET(d,NULL)==-1&&allocations==before&&d->refcount==1);cases++;
for(unsigned kind=0;kind<2;kind++){fail_at=allocations+1;out=(void*)(uintptr_t)1;CHECK((kind?DEPTH(d,&out):BACK(d,0,&out))==-2&&!out&&d->refcount==1&&!d->child_parent_refs);fail_at=0;cases++;
Interface*a=NULL,*b=NULL;CHECK(!(kind?DEPTH(d,&a):BACK(d,0,&a))&&a&&d->refcount==2&&d->child_parent_refs==1);before=allocations;CHECK(!(kind?DEPTH(d,&b):BACK(d,0,&b))&&a==b&&d->refcount==2&&d->child_parent_refs==1&&allocations==before);cases++;
CHECK(a->lpVtbl->AddRef(a)==3&&d->refcount==2&&d->child_parent_refs==1);CHECK(a->lpVtbl->Release(a)==2&&b->lpVtbl->Release(b)==1&&d->refcount==2);cases++;
CHECK(a->lpVtbl->Release(a)==0&&d->refcount==1&&!d->child_parent_refs);CHECK(a->lpVtbl->Release(a)==0&&a->lpVtbl->Release(a)==0&&d->refcount==1&&!d->child_parent_refs);cases++;
CHECK(a->lpVtbl->AddRef(a)==1&&d->refcount==2&&d->child_parent_refs==1);CHECK(a->lpVtbl->Release(a)==0&&d->refcount==1);cases++;
fail_at=allocations+1;CHECK(!(kind?DEPTH(d,&b):BACK(d,0,&b))&&a==b&&allocations+1==fail_at&&d->refcount==2);fail_at=0;CHECK(b->lpVtbl->Release(b)==0&&d->refcount==1);cases++;
${api===9?`if(kind){void*q=NULL;CHECK(!surface_get_container(a,&unknown_id,&q)&&q==&d->iface&&d->refcount==2);CHECK(((Interface*)q)->lpVtbl->Release(q)==1);q=(void*)(uintptr_t)1;CHECK(surface_get_container(a,&bad_iid,&q)==-4&&!q&&d->refcount==1);cases++;}`:''}
if(!kind){for(UINT type=0;type<4;type++){DWORD value=type==3?0xdeadbeef:type;CHECK(!BACK(d,(int)value,&b)&&b==a&&d->refcount==2);CHECK(b->lpVtbl->Release(b)==0&&d->refcount==1);cases++;}CHECK(!TARGET(d,&b)&&b==a&&b->lpVtbl->Release(b)==0);cases++;}
}
Surface*bound_back=d->implicit_back_buffer,*bound_depth=d->implicit_depth_stencil;
CHECK(!bound_back->refcount&&!bound_depth->refcount&&d->refcount==1);
surface_binding_add_ref(bound_back);surface_binding_add_ref(bound_depth);
${api===8?'d->depth_surface=bound_depth;':'d->render_target_surfaces[0]=bound_back;d->depth_stencil_surface=bound_depth;surface_binding_add_ref(bound_back);d->render_target_surfaces[1]=bound_back;'}
CHECK(!bound_back->refcount&&!bound_depth->refcount&&d->refcount==1&&!d->child_parent_refs);
${api===8?'CHECK(!device_has_reset_blockers(d));':'CHECK(bound_back->binding_refs==2&&bound_depth->binding_refs==1&&!reset_surface_owned_refs(d,bound_back)&&!reset_surface_owned_refs(d,bound_depth));'}
for(UINT cycle=0;cycle<128;cycle++){
 Interface*color=NULL,*depth=NULL;CHECK(!TARGET(d,&color)&&!DEPTH(d,&depth)&&color==&bound_back->iface&&depth==&bound_depth->iface&&d->refcount==3&&d->child_parent_refs==2);
 surface_binding_add_ref(bound_back);surface_binding_add_ref(bound_depth);surface_binding_release(bound_back);surface_binding_release(bound_depth);CHECK(bound_back->refcount==1&&bound_depth->refcount==1&&d->refcount==3);
 ${api===8?'CHECK(device_has_reset_blockers(d));':'CHECK(!reset_surface_owned_refs(d,bound_back)&&!reset_surface_owned_refs(d,bound_depth));'}
 CHECK(color->lpVtbl->Release(color)==0&&depth->lpVtbl->Release(depth)==0&&d->refcount==1&&!d->child_parent_refs);
 surface_binding_add_ref(bound_back);surface_binding_release(bound_back);surface_binding_add_ref(bound_depth);surface_binding_release(bound_depth);
 CHECK(!bound_back->refcount&&!bound_depth->refcount&&d->refcount==1);
}
device_release_owned_references(d);CHECK(!bound_back->refcount&&!bound_depth->refcount&&!bound_back->binding_refs&&!bound_depth->binding_refs&&d->refcount==1&&live==3);
printf("PASS actual API ${api} implicit bindings: 128 rounds, 256 public-zero revivals; cached color/depth private holds, repeated binding, one parent pin per public surface, public-only Reset accounting and cache storage after last unbind\\n");
${api===8?`CHECK(!device_has_reset_blockers(d));Interface*depth=NULL;CHECK(!DEPTH(d,&depth)&&device_has_reset_blockers(d));d->depth_surface=(Surface*)depth;CHECK(device_has_reset_blockers(d));CHECK(depth->lpVtbl->AddRef(depth)==2&&device_has_reset_blockers(d));CHECK(depth->lpVtbl->Release(depth)==1&&device_has_reset_blockers(d));d->depth_surface=NULL;CHECK(depth->lpVtbl->Release(depth)==0&&!device_has_reset_blockers(d));cases++;`:''}
Interface*a=NULL,*b=NULL;CHECK(!BACK(d,0,&a)&&!DEPTH(d,&b)&&a!=b&&d->refcount==3&&d->child_parent_refs==2);CHECK(device_release(&d->iface)==2&&destroys==0);CHECK(a->lpVtbl->Release(a)==0&&d->refcount==1&&d->child_parent_refs==1&&!destroys);CHECK(b->lpVtbl->Release(b)==0&&!live&&destroys==1&&!parent_refs);cases++;
printf("PASS actual API ${api} implicit identity: %u lifecycle groups, repeated/revived/zero Release identity, one device pin per active surface, ignored stereo types, failed allocation atomicity and last-surface teardown\\n",cases);return 0;}
`);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root,dir+'/implicit.c',root+'/runtime/d9wg_private_data.c','-o',dir+'/implicit'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/implicit',[],{encoding:'utf8',timeout:10000});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}}finally{fs.rmSync(dir,{recursive:true,force:true});}
