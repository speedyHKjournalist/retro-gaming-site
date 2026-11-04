"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D9WG_RECORD_OBJECT_SOURCE||path.join(root,"d3d9proxy/d3d9_proxy.c"),"utf8");
function extract(name){const m=new RegExp("static (?:HRESULT(?: WINAPI)?|BOOL|void) "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const names=['texture_binding_slot',...['texture','stream_source','indices','vertex_shader','pixel_shader','vertex_declaration','fvf','light'].flatMap(n=>['device_set_'+n,'device_get_'+n]),'d9_emit_light','device_light_enable','device_get_light_enable','state_block_release_references','device_begin_state_block','device_end_state_block'];
// The SetLight function calls this emitter, so put its definition first.
names.splice(names.indexOf('d9_emit_light'),1);names.unshift('d9_emit_light');
const interfaces=['VertexBuffer','IndexBuffer','BaseTexture','Texture','CubeTexture','VolumeTexture','VertexDeclaration','VertexShader','PixelShader','StateBlock'].map(n=>'typedef Interface IDirect3D'+n+'9;').join('\n');
const code=`#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "${path.join(root,'d3d9proxy/d3d9_protocol.h')}"
typedef int BOOL,WINBOOL,HRESULT;typedef uint32_t UINT,DWORD;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define TRACE_REFUSE(x) (x)
#define HOSTLOG_FAILED(...) ((void)0)
#define PURE_DEVICE_REFUSE(...) ((void)0)
#define CopyMemory memcpy
#define D9_MAX_TEXTURE_BINDINGS 20u
#define D9_MAX_STREAMS 16u
#define D9_MAX_LIGHTS 8u
#define D3DVERTEXTEXTURESAMPLER0 257u
#define D3DMAXDECLLENGTH 64u
#define D3DLIGHT_POINT 1
#define D3DLIGHT_DIRECTIONAL 3
typedef struct D3DLIGHT9 {uint32_t Type;float data[25];} D3DLIGHT9;
#define D9WG_LIGHTING_SDK_LIGHT D3DLIGHT9
static int fail_light;static unsigned allocations,frees;
static void *light_alloc(size_t n){if(fail_light)return NULL;allocations++;return calloc(1,n);}
static void light_free(void *p){frees++;free(p);}
#define D9WG_LIGHT_ALLOC(n) light_alloc(n)
#define D9WG_LIGHT_FREE(p) light_free(p)
#include "${path.join(root,'runtime/d9wg_light_state.h')}"
typedef struct Interface {const void *lpVtbl;} Interface;
${interfaces}
typedef struct D9Device D9Device;
typedef struct Object {Interface iface;D9Device *device;uint32_t handle;UINT length;int refs;} Object;
typedef Object D9Texture,D9CubeTexture,D9VolumeTexture,D9VertexBuffer,D9IndexBuffer,D9VertexDeclaration;
typedef struct D9Shader {union {Interface vertex,pixel;} iface;D9Device *device;uint32_t handle;UINT length;int refs;} D9Shader;
typedef struct D9StreamBinding {D9VertexBuffer *buffer;UINT stride,offset;DWORD frequency;} D9StreamBinding;
typedef struct D9StateBlock {Interface iface;D9Device *device;D9Texture *textures[20];D9CubeTexture *cube_textures[20];D9VolumeTexture *volume_textures[20];D9Shader *vertex_shader,*pixel_shader;D9VertexDeclaration *vertex_declaration;D9StreamBinding streams[16];D9IndexBuffer *index_buffer;BOOL has_texture[20],has_stream[16],has_indices,has_vertex_shader,has_pixel_shader,has_vertex_format;DWORD fvf;BOOL has_light[8],has_light_enable[8],light_enabled[8];D3DLIGHT9 lights[8];D9WGLightState *extra_lights;} D9StateBlock;
struct D9Device {D9Texture *textures[20];D9CubeTexture *cube_bindings[20];D9VolumeTexture *volume_bindings[20];D9Shader *vertex_shader,*pixel_shader;D9VertexDeclaration *vertex_declaration;D9StreamBinding streams[16];D9IndexBuffer *index_buffer;DWORD fvf;D9StateBlock *recording_result;D3DLIGHT9 lights[8];BOOL light_set[8],light_enabled[8];D9WGLightState *extra_lights;uint32_t handle;};
typedef D9Device IDirect3DDevice9;
static const int g_texture_vtbl=1,g_cube_vtbl=2,g_volume_texture_vtbl=3,g_vb_vtbl=4,g_ib_vtbl=5,g_decl_vtbl=6,g_vertex_shader_vtbl=7,g_pixel_shader_vtbl=8;
static struct {void *iface;int *refs;} registry[32];static unsigned registry_count,emissions;static int fail_alloc;
static int addref(void *iface){for(unsigned i=0;i<registry_count;++i)if(registry[i].iface==iface){if(*registry[i].refs<=0)abort();return ++*registry[i].refs;}abort();}
static int release(void *iface){for(unsigned i=0;i<registry_count;++i)if(registry[i].iface==iface){if(*registry[i].refs<=0)abort();return --*registry[i].refs;}abort();}
/* These are backend construction stubs; public recording references remain
 * independently observed. The complete private lifetime runs in its own suite. */
static void decl_binding_add_ref(D9VertexDeclaration*d){addref(&d->iface);}
static void decl_binding_release(D9VertexDeclaration*d){release(&d->iface);}
static void shader_binding_add_ref(D9Shader*s){addref(&s->iface);}
static void shader_binding_release(D9Shader*s){release(&s->iface);}
static void vb_binding_add_ref(D9VertexBuffer*b){addref(&b->iface);}
static void vb_binding_release(D9VertexBuffer*b){release(&b->iface);}
static void ib_binding_add_ref(D9IndexBuffer*b){addref(&b->iface);}
static void ib_binding_release(D9IndexBuffer*b){release(&b->iface);}
static void volume_texture_binding_add_ref(D9VolumeTexture*t){addref(&t->iface);}
static void volume_texture_binding_release(D9VolumeTexture*t){release(&t->iface);}
static void cube_binding_add_ref(D9CubeTexture*t){addref(&t->iface);}
static void cube_binding_release(D9CubeTexture*t){release(&t->iface);}
static void texture_binding_add_ref(D9Texture*t){addref(&t->iface);}
static void texture_binding_release(D9Texture*t){release(&t->iface);}

${['VertexBuffer','IndexBuffer','Texture','CubeTexture','VolumeTexture','VertexDeclaration','VertexShader','PixelShader','BaseTexture'].map(n=>'#define IDirect3D'+n+'9_AddRef(p) addref(p)\n#define IDirect3D'+n+'9_Release(p) release(p)').join('\n')}
static D9Device *device_from_iface(D9Device *p){return p;}
static D9VertexBuffer *vb_from_iface(Interface *p){return (void*)p;}
static D9IndexBuffer *ib_from_iface(Interface *p){return (void*)p;}
static D9VertexDeclaration *decl_from_iface(Interface *p){return (void*)p;}
static D9Shader *vertex_shader_from_iface(Interface *p){return (void*)p;}
static D9Shader *pixel_shader_from_iface(Interface *p){return (void*)p;}
static BOOL emit_command(uint32_t op,const void *p,uint32_t n){(void)op;(void)p;(void)n;emissions++;return TRUE;}
static HRESULT emit_set_shader(D9Device *d,D9Shader *s,BOOL pixel){(void)d;(void)s;(void)pixel;emissions++;return D3D_OK;}
static BOOL emit_set_fvf(D9Device *d,DWORD fvf,const D9WGVertexElement *elements,UINT count){(void)d;(void)fvf;(void)elements;(void)count;emissions++;return TRUE;}
static BOOL fvf_to_declaration(DWORD fvf,D9WGVertexElement *elements,UINT *count){if(fvf!=2)return FALSE;memset(elements,0,sizeof(*elements));*count=1;return TRUE;}
static D9StateBlock recording;
static D9StateBlock *state_block_alloc(D9Device *d){if(fail_alloc)return NULL;memset(&recording,0,sizeof(recording));recording.device=d;return &recording;}
${names.map(extract).join('\n')}
static Object *make(Object *o,D9Device *d,const void *vtbl,uint32_t handle){*o=(Object){{vtbl},d,handle,64,1};registry[registry_count].iface=&o->iface;registry[registry_count++].refs=&o->refs;return o;}
static void make_shader(D9Shader *s,D9Device *d,const void *vtbl,uint32_t handle){memset(s,0,sizeof(*s));s->iface.vertex.lpVtbl=vtbl;s->device=d;s->handle=handle;s->length=64;s->refs=1;registry[registry_count].iface=&s->iface;registry[registry_count++].refs=&s->refs;}
static int refcount(void *p){for(unsigned i=0;i<registry_count;++i)if(registry[i].iface==p)return *registry[i].refs;abort();}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"D3D9 object recording line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){D9Device device={0};Object old_vb,new_vb,old_ib,new_ib,old_decl,new_decl,texture,cube,volume;D9Shader old_vs,new_vs,old_ps,new_ps;make(&old_vb,&device,&g_vb_vtbl,1);make(&new_vb,&device,&g_vb_vtbl,2);make(&old_ib,&device,&g_ib_vtbl,3);make(&new_ib,&device,&g_ib_vtbl,4);make(&old_decl,&device,&g_decl_vtbl,5);make(&new_decl,&device,&g_decl_vtbl,6);make(&texture,&device,&g_texture_vtbl,7);make(&cube,&device,&g_cube_vtbl,8);make(&volume,&device,&g_volume_texture_vtbl,9);make_shader(&old_vs,&device,&g_vertex_shader_vtbl,10);make_shader(&new_vs,&device,&g_vertex_shader_vtbl,11);make_shader(&old_ps,&device,&g_pixel_shader_vtbl,12);make_shader(&new_ps,&device,&g_pixel_shader_vtbl,13);
 CHECK(device_set_stream_source(&device,1,&old_vb.iface,0,12)==0&&device_set_indices(&device,&old_ib.iface)==0&&device_set_vertex_declaration(&device,&old_decl.iface)==0&&device_set_vertex_shader(&device,&old_vs.iface.vertex)==0&&device_set_pixel_shader(&device,&old_ps.iface.pixel)==0);D3DLIGHT9 light={0};light.Type=3;light.data[0]=.75f;CHECK(device_set_light(&device,37,&light)==0&&device_light_enable(&device,37,TRUE)==0);
 unsigned sent=emissions;CHECK(device_begin_state_block(&device)==0);D9Device before=device;CHECK(device_begin_state_block(&device)==-1);
 CHECK(device_set_stream_source(&device,1,&new_vb.iface,4,12)==0&&device_set_stream_source(&device,1,&new_vb.iface,4,12)==0&&new_vb.refs==2);CHECK(device_set_indices(&device,&new_ib.iface)==0&&new_ib.refs==2);
 CHECK(device_set_texture(&device,0,&texture.iface)==0&&texture.refs==2);CHECK(device_set_texture(&device,0,&cube.iface)==0&&texture.refs==1&&cube.refs==2);CHECK(device_set_texture(&device,0,&volume.iface)==0&&cube.refs==1&&volume.refs==2);CHECK(device_set_texture(&device,0,&texture.iface)==0&&volume.refs==1);CHECK(device_set_texture(&device,1,&cube.iface)==0&&device_set_texture(&device,260,&volume.iface)==0);CHECK(texture.refs==2&&cube.refs==2&&volume.refs==2);
 CHECK(device_set_vertex_shader(&device,&new_vs.iface.vertex)==0&&device_set_pixel_shader(&device,&new_ps.iface.pixel)==0&&new_vs.refs==2&&new_ps.refs==2);CHECK(device_set_vertex_declaration(&device,&new_decl.iface)==0&&new_decl.refs==2);CHECK(device_set_fvf(&device,2)==0&&new_decl.refs==1&&recording.fvf==2);CHECK(device_set_vertex_declaration(&device,&new_decl.iface)==0&&new_decl.refs==2&&recording.fvf==0);
 Interface *got;UINT offset,stride;DWORD fvf;CHECK(device_get_stream_source(&device,1,&got,&offset,&stride)==0&&got==&old_vb.iface&&offset==0&&stride==12);release(got);CHECK(device_get_indices(&device,&got)==0&&got==&old_ib.iface);release(got);CHECK(device_get_texture(&device,0,&got)==0&&!got);CHECK(device_get_vertex_shader(&device,&got)==0&&got==&old_vs.iface.vertex);release(got);CHECK(device_get_pixel_shader(&device,&got)==0&&got==&old_ps.iface.pixel);release(got);CHECK(device_get_vertex_declaration(&device,&got)==0&&got==&old_decl.iface);release(got);CHECK(device_get_fvf(&device,&fvf)==0&&fvf==0);
 light.data[0]=.125f;CHECK(device_set_light(&device,3,&light)==0&&recording.has_light[3]);D3DLIGHT9 low;memset(&low,0xcc,sizeof(low));CHECK(device_get_light(&device,3,&low)==-1);CHECK(device_set_light(&device,37,&light)==0&&device_light_enable(&device,37,FALSE)==0);D3DLIGHT9 queried;BOOL active;CHECK(device_get_light(&device,37,&queried)==0&&queried.data[0]==.75f&&device_get_light_enable(&device,37,&active)==0&&active);CHECK(device_light_enable(&device,1002,FALSE)==0);memset(&queried,0xcc,sizeof(queried));CHECK(device_get_light(&device,1002,&queried)==-1);D9WGLightState *entry=d9wg_light_find(recording.extra_lights,1002);CHECK(entry&&entry->set&&entry->light_mask&&entry->enable_mask&&!entry->enabled&&entry->light.Type==3&&entry->light.data[0]==1&&entry->light.data[17]==1);fail_light=1;CHECK(device_light_enable(&device,1003,TRUE)==-3&&!d9wg_light_find(recording.extra_lights,1003));fail_light=0;CHECK(!memcmp(&device,&before,sizeof(device))&&emissions==sent);
 CHECK(device_set_stream_source(&device,1,&new_vb.iface,64,12)==-1&&device_set_texture(&device,256,&texture.iface)==-1&&device_set_fvf(&device,0xffffffffu)==-1);Interface *result=(void*)1;CHECK(device_end_state_block(&device,NULL)==-1&&device.recording_result==&recording);CHECK(device_end_state_block(&device,&result)==0&&result==&recording.iface&&!device.recording_result&&emissions==sent);CHECK(device_end_state_block(&device,&result)==-1);
 /* Drop the app references; Apply setters and snapshot release now transfer
  * the last references to the live bindings, which finally release them. */
 void *recorded[]={&new_vb,&new_ib,&new_decl,&new_vs,&new_ps,&texture,&cube,&volume};for(unsigned i=0;i<8;++i)CHECK(release(recorded[i])==1);
 CHECK(device_set_stream_source(&device,1,&new_vb.iface,4,12)==0&&device_set_indices(&device,&new_ib.iface)==0&&device_set_vertex_shader(&device,&new_vs.iface.vertex)==0&&device_set_pixel_shader(&device,&new_ps.iface.pixel)==0&&device_set_vertex_declaration(&device,&new_decl.iface)==0);CHECK(device_set_texture(&device,0,&texture.iface)==0&&device_set_texture(&device,1,&cube.iface)==0&&device_set_texture(&device,260,&volume.iface)==0);state_block_release_references(&recording);d9wg_light_free(&recording.extra_lights);CHECK(device_set_stream_source(&device,1,NULL,0,0)==0&&device_set_indices(&device,NULL)==0&&device_set_vertex_shader(&device,NULL)==0&&device_set_pixel_shader(&device,NULL)==0&&device_set_vertex_declaration(&device,NULL)==0);CHECK(device_set_texture(&device,0,NULL)==0&&device_set_texture(&device,1,NULL)==0&&device_set_texture(&device,260,NULL)==0);for(unsigned i=0;i<8;++i)CHECK(refcount(recorded[i])==0);
 void *original[]={&old_vb,&old_ib,&old_decl,&old_vs,&old_ps};for(unsigned i=0;i<5;++i)CHECK(release(original[i])==0);d9wg_light_free(&device.extra_lights);CHECK(allocations==frees);fail_alloc=1;CHECK(device_begin_state_block(&device)==-3&&!device.recording_result);return 0;}
`;
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-record-objects-"));try{const input=path.join(directory,"objects.c"),binary=path.join(directory,"objects");fs.writeFileSync(input,code);const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",input,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr||String(built.error));const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));console.log("PASS actual D3D9 object C recording: live queries, exact VB/IB/texture/shader/declaration refs, kind replacement, sparse default lights, atomic OOM, Begin/End isolation and final release");}finally{fs.rmSync(directory,{recursive:true,force:true});}
