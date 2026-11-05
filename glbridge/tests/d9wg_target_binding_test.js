"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-target-binding-"));
function extract(source, name) {
    const m = new RegExp((name.startsWith("d9wg_d3d9_core_") ? "HRESULT WINAPI " : "static ") + "[^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source);
    // Old-source controls still execute the real legacy setters and public
    // methods; absent new helpers are unreachable compatibility declarations.
    const compatibility = {
        device_clear_state_block_handles8: "static void device_clear_state_block_handles8(D9Device*d){(void)d;}",
        surface_destroy_storage: "static void surface_destroy_storage(D9Surface*s){(void)s;assert(0);}",
        surface_uses_private_bindings: "static BOOL surface_uses_private_bindings(const D9Surface*s){(void)s;return FALSE;}",
        surface_binding_add_ref: "static void surface_binding_add_ref(D9Surface*s){IDirect3DSurface9_AddRef(&s->iface);}",
        surface_binding_release: "static void surface_binding_release(D9Surface*s){IDirect3DSurface9_Release(&s->iface);}",
    };
    if (!m && compatibility[name]) return compatibility[name];
    if (!m && ["clear_bindings_after_up_missing_format", "device_clear_fvf_cache", "device_fvf_declaration", "device_set_stream_binding", "decl_destroy_storage", "decl_binding_add_ref", "decl_binding_release", "shader_destroy_storage", "shader_binding_add_ref", "shader_binding_release", "vb_destroy_storage", "vb_binding_add_ref", "vb_binding_release", "ib_destroy_storage", "ib_binding_add_ref", "ib_binding_release", "volume_texture_destroy_storage", "volume_texture_binding_add_ref", "volume_texture_binding_release", "cube_destroy_storage", "cube_binding_add_ref", "cube_binding_release", "texture_destroy_storage", "texture_binding_add_ref", "texture_binding_release"].includes(name))
        return extract(fs.readFileSync(root + "/d3d9proxy/d3d9_proxy.c", "utf8"), name);
    assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;}
    return source.slice(m.index, end);
}
const deviceCleanupBindings=process.env.D9WG_TEST_DEVICE_CLEANUP8 === "1";
const shaderStorageBindings=deviceCleanupBindings || process.env.D9WG_TEST_SHADER_STORAGE8 === "1";
const deviceViewBindings=shaderStorageBindings || process.env.D9WG_TEST_DEVICE_VIEWS8 === "1";
const viewBindings = deviceViewBindings || process.env.D9WG_TEST_OBJECT_VIEWS8 === "1";
const incrementalBindings = viewBindings || process.env.D9WG_TEST_INCREMENTAL_BLOCK8 === "1";
const tokenBindings = incrementalBindings || process.env.D9WG_TEST_STATE_BLOCK_HANDLE8 === "1";
const snapshotBindings = tokenBindings || process.env.D9WG_TEST_SNAPSHOT_BLOCK8 === "1";
const legacyProgramBindings = snapshotBindings || process.env.D9WG_TEST_LEGACY_PROGRAM_BINDINGS === "1";
const legacyFvfBindings = legacyProgramBindings || process.env.D9WG_TEST_LEGACY_FVF_BINDINGS === "1";
const zeroUp = process.env.D9WG_TEST_ZERO_UP === "1";
const upBindings = zeroUp || process.env.D9WG_TEST_UP_FAILURE === "1";
const declarationBindings = legacyFvfBindings || upBindings || process.env.D9WG_TEST_DECLARATION_FVF === "1";
const fvfBindings = declarationBindings || process.env.D9WG_TEST_FVF_BINDINGS === "1";
const streamBindings = fvfBindings || process.env.D9WG_TEST_STREAM_BINDINGS === "1";
const rootBindings = streamBindings || process.env.D9WG_TEST_ROOT_BINDINGS === "1";
const stateBindings = rootBindings || process.env.D9WG_TEST_STATE_BINDINGS === "1";
const shaderBindings = stateBindings || process.env.D9WG_TEST_SHADER_BINDINGS === "1";
const bufferBindings = shaderBindings || process.env.D9WG_TEST_BUFFER_BINDINGS === "1";
const volumeBindings = bufferBindings || process.env.D9WG_TEST_VOLUME_BINDINGS === "1";
const cubeBindings = volumeBindings || process.env.D9WG_TEST_CUBE_BINDINGS === "1";
const textureBindings = cubeBindings || process.env.D9WG_TEST_TEXTURE_BINDINGS === "1";
try {
    const source = fs.readFileSync(process.env.D9WG_TARGET_BINDING_SOURCE || root + "/d3d9proxy/d3d9_proxy.c", "utf8");
    const blockDefinition = stateBindings ? "typedef struct D9StateBlock D9StateBlock;\n" + source.match(/struct D9StateBlock \{[\s\S]*?\n\};/)[0] : "";
    const stateFields = `DWORD render_states[256],texture_stage_states[8][33],sampler_states[20][14];float transforms[512][16],clip_planes[6][4],npatch_segments;D3DMATERIAL9 material;D3DVIEWPORT9 viewport;RECT scissor_rect;D3DLIGHT9 lights[8];BOOL light_set[8],light_enabled[8];UINT current_palette;float vs_const_f[256][4],ps_const_f[224][4];int vs_const_i[16][4],ps_const_i[16][4];BOOL vs_const_b[16],ps_const_b[16];`;
    fs.writeFileSync(dir + "/target.c", `#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_private_data.h"
#include "runtime/d9wg_reset.h"
#include "runtime/d9wg_fvf.h"
typedef uint32_t UINT,DWORD,ULONG,D3DMULTISAMPLE_TYPE;typedef int32_t LONG,HRESULT;typedef int BOOL,D3DFORMAT,D3DPOOL,D3DCUBEMAP_FACES,D3DPRIMITIVETYPE;typedef uint8_t BYTE;
#define D9_MAX_RENDER_TARGETS 4
#define D9_MAX_STREAMS 16
#define D9_MAX_TEXTURE_BINDINGS 20
#define D3DERR_NOTFOUND (-4)
#define D3DBACKBUFFER_TYPE_MONO 0
#define SUCCEEDED(v) ((v)>=0)
#define FAILED(v) ((v)<0)
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define E_OUTOFMEMORY (-3)
#define D3DPOOL_DEFAULT 0
#define D3DMULTISAMPLE_NONE 0
#define D3DVERTEXTEXTURESAMPLER0 257
#define PURE_DEVICE_REFUSE(...)
#define D3DUSAGE_RENDERTARGET 1
#define D3DUSAGE_DEPTHSTENCIL 2
#define HEAP_ZERO_MEMORY 8
#define D3DFMT_INDEX16 101
#define D3DFMT_INDEX32 102
#define D3DPT_POINTLIST 1
#define D3DPT_LINELIST 2
#define D3DPT_LINESTRIP 3
#define D3DPT_TRIANGLELIST 4
#define D3DPT_TRIANGLESTRIP 5
#define D3DPT_TRIANGLEFAN 6
#define ZeroMemory(p,n) memset(p,0,n)
#define TRACE_FRAME_REJECT(...)
#define TRACE_FRAME_DRAW(...)
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define TRACE_MARK_ENTER(...) trace_enter(__VA_ARGS__)
#define TRACE_MARK_EXIT(...) trace_exit(__VA_ARGS__)
#define TRACE_REGISTER_RANGE(...)
#define TRACE_SURFACE_FREED(...)
#define TRACE_FLUSH(...)
typedef struct{const void*lpVtbl;}Interface;typedef Interface IDirect3DDevice9,IDirect3DSurface9,IDirect3DTexture9,IDirect3DBaseTexture9,IDirect3DCubeTexture9,IDirect3DVolumeTexture9,IDirect3DVolume9,IDirect3DVertexBuffer9,IDirect3DIndexBuffer9,IDirect3DVertexDeclaration9,IDirect3DVertexShader9,IDirect3DPixelShader9,IDirect3DStateBlock9,IUnknown;
${snapshotBindings?"typedef HRESULT (*D9WGStateBlockRecorder8)(IDirect3DDevice9*,const void*);":""}
typedef struct Device D9Device;typedef struct Surface D9Surface,D9Volume;typedef struct Texture D9Texture,D9CubeTexture,D9VolumeTexture,D9VertexBuffer,D9IndexBuffer;typedef struct Chain{Interface iface;D9Device*device;LONG refcount;UINT handle;struct Chain*next_device_chain;}D9SwapChain;
typedef struct{UINT width,height,row_pitch,row_count,byte_count;BYTE*shadow;BOOL shadow_valid,locked;D9WGPrivateStore private_data;D9Surface*level_surface,*level_volume;}D9TextureLevel;
struct Texture{Interface iface;LONG refcount,binding_refs;D9Device*device;UINT handle,width,height,level_count,usage,length,fvf;BYTE*shadow;void*clips;D3DFORMAT format;D3DPOOL pool;D3DMULTISAMPLE_TYPE multisample;DWORD multisample_quality;D9TextureLevel*levels;D9Texture*next_device_resource;BOOL private_back_buffer;D9WGPrivateStore private_data;BOOL locked;};
struct Surface{Interface iface;LONG refcount,binding_refs;D9Device*device;D9Texture*texture,*cube_texture;D9SwapChain*swap_chain;BOOL texture_child,auto_depth_stencil,backbuffer,device_pin_transferred,locked;UINT level,width,height,cube_face;D3DFORMAT format;D3DPOOL pool;D3DMULTISAMPLE_TYPE multisample;DWORD multisample_quality;BYTE*shadow;void*dc,*dc_previous,*dib;D9WGPrivateStore private_data;};
typedef struct{Interface iface;LONG refs;}Parent;
typedef struct{UINT X,Y,Width,Height;float MinZ,MaxZ;}D3DVIEWPORT9;
typedef struct{int left,top,right,bottom;}RECT;
typedef struct{uint16_t Stream,Offset;BYTE Type,Method,Usage,UsageIndex;}D3DVERTEXELEMENT9;
#define D3DDECLTYPE_FLOAT1 0
#define D3DDECLTYPE_FLOAT2 1
#define D3DDECLTYPE_FLOAT3 2
#define D3DDECLTYPE_FLOAT4 3
#define D3DDECLTYPE_D3DCOLOR 4
#define D3DDECLTYPE_UBYTE4 5
#define D3DDECLTYPE_SHORT2 6
#define D3DDECLTYPE_SHORT4 7
#define D3DDECLTYPE_UBYTE4N 8
#define D3DDECLTYPE_SHORT2N 9
#define D3DDECLTYPE_SHORT4N 10
#define D3DDECLTYPE_USHORT2N 11
#define D3DDECLTYPE_USHORT4N 12
#define D3DDECLTYPE_UDEC3 13
#define D3DDECLTYPE_DEC3N 14
#define D3DDECLTYPE_FLOAT16_2 15
#define D3DDECLTYPE_FLOAT16_4 16
#define D3DDECLTYPE_UNUSED 17
#define D3DDECLUSAGE_POSITION 0
#define D3DDECLUSAGE_BLENDWEIGHT 1
#define D3DDECLUSAGE_BLENDINDICES 2
#define D3DDECLUSAGE_NORMAL 3
#define D3DDECLUSAGE_PSIZE 4
#define D3DDECLUSAGE_TEXCOORD 5
#define D3DDECLUSAGE_TANGENT 6
#define D3DDECLUSAGE_BINORMAL 7
#define D3DDECLUSAGE_TESSFACTOR 8
#define D3DDECLUSAGE_POSITIONT 9
#define D3DDECLUSAGE_COLOR 10
#define D3DDECLUSAGE_FOG 11
#define D3DDECLUSAGE_DEPTH 12
#define D3DDECLUSAGE_SAMPLE 13
#define D3DDECLMETHOD_DEFAULT 0
#define D3DFVF_XYZ 2
#define D3DFVF_XYZRHW 4
#define D3DMAXDECLLENGTH 18
#define D3DDECL_END() ((D3DVERTEXELEMENT9){255,0,17,0,0,0})
#define CopyMemory memcpy
#define HOSTLOG_REFUSED(...)
#define HOSTLOG_FAILED(...)
#define S_OK 0
#define E_POINTER (-5)
#define E_NOINTERFACE (-6)
typedef const void*REFIID;
typedef struct Declaration{Interface iface;LONG refcount,binding_refs;D9Device*device;UINT handle,element_count;DWORD fvf,legacy_fvf8;BOOL fvf_converted,fvf_cache_ref;D3DVERTEXELEMENT9 elements[18];struct Declaration*next_device_resource;}D9VertexDeclaration;
typedef struct Shader{union{Interface vertex,pixel;}iface;LONG refcount,binding_refs;D9Device*device;UINT handle;BOOL is_pixel;DWORD*code;UINT token_count;struct Shader*next_device_resource;}D9Shader;
typedef struct{D9VertexBuffer*buffer;UINT stride,offset,frequency;}D9StreamBinding;
typedef struct{float values[17];}D3DMATERIAL9;
typedef struct{float values[26];}D3DLIGHT9;
typedef struct D9WGLightState{struct D9WGLightState*next;UINT index;BOOL light_mask,enable_mask,set,enabled;D3DLIGHT9 light;}D9WGLightState;
${stateBindings?`#define D9_MAX_RENDER_STATES 256
#define D9_MAX_TEXTURE_STAGES 8
#define D9_MAX_TEXTURE_STAGE_STATES 33
#define D9_MAX_SAMPLERS 20
#define D9_MAX_SAMPLER_STATES 14
#define D9_STATE_BLOCK_TRANSFORMS 266
#define D9_MAX_CLIP_PLANES 6
#define D9_MAX_LIGHTS 8
#define D9_MAX_VS_CONST_F 256
#define D9_MAX_PS_CONST_F 224
#define D9_MAX_CONST_I 16
#define D9_MAX_CONST_B 16
`+blockDefinition:`typedef struct D9StateBlock{D9Texture*textures[20],*cube_textures[20],*volume_textures[20],*index_buffer;D9StreamBinding streams[16];BOOL has_texture[20],has_stream[16],has_indices,has_vertex_format,has_vertex_shader,has_pixel_shader;D9VertexDeclaration*vertex_declaration;D9Shader*vertex_shader,*pixel_shader;DWORD fvf;}D9StateBlock;`}

typedef struct{float X,Y,Width,Height,MinZ,MaxZ;}D9WGProcessViewport;
typedef struct{UINT clip_union,clip_intersection;}D9WGClipStatus;
typedef struct{const void*vertices,*indices;UINT length,stride,count,vertex_count,status_screen,index_bytes,index_stride,min_vertex;D9WGProcessViewport viewport;}D9WGClipDrawSource;
struct Device{Interface iface;D9WGClipStatus clip_status;BOOL clip_status_valid;D9StateBlock*recording_result;LONG refcount,child_parent_refs;BOOL releasing_owned_refs;UINT handle;Parent*parent;D9Texture*texture_resources;D9Surface*implicit_back_buffer,*implicit_depth_stencil,*render_target_surfaces[4],*depth_stencil_surface;BOOL depth_stencil_unbound;void*extra_lights;int palettes;D9Texture*vertex_buffers,*index_buffers,*cube_textures,*volume_textures,*textures[20],*cube_bindings[20],*volume_bindings[20],*index_buffer;D9StreamBinding streams[16];D9VertexDeclaration*vertex_declaration,*vertex_declarations;D9Shader*vertex_shader,*pixel_shader,*shaders;UINT fvf;struct{UINT BackBufferWidth,BackBufferHeight,BackBufferFormat,MultiSampleType,MultiSampleQuality,AutoDepthStencilFormat;BOOL EnableAutoDepthStencil;}present;D9SwapChain*additional_swap_chains;D9StateBlock*state_blocks;${stateBindings?stateFields:""}};
${tokenBindings?`static void*token_private_alloc(void*,uint32_t);static void token_private_free(void*,void*);static void token_private_retain(void*,void*);static void token_private_release(void*,void*);
static const D9WGPrivateOps d9wg_private_ops={NULL,token_private_alloc,token_private_free,token_private_retain,token_private_release,NULL,NULL};`:'static const D9WGPrivateOps d9wg_private_ops={0};'}static int g_texture_vtbl,g_surface_vtbl,g_transport_lock;
${deviceViewBindings?"#define D8WG_NATIVE_DEVICE_VIEWS8 1\nstatic void(*device_view_finalizer_hook)(void*);":""}
${viewBindings?"static int g_swap_chain_vtbl,g_device_vtbl;static UINT view_lock_depth;typedef void(*D9WGCoreViewRetire8)(void*);typedef void(*D9WGCoreDeviceCleanup8)(void*);":""}
${viewBindings?source.match(/typedef struct D9CoreView8 \{[\s\S]*?\n\} D9CoreView8;/)[0]+"\nstatic D9CoreView8*g_core_views8;\nstatic void core_view_retire8(void*);":"#define core_view_retire8(object) ((void)(object))"}
${textureBindings?"static int g_cube_vtbl,g_volume_texture_vtbl;":""}
${bufferBindings?"static int g_vb_vtbl,g_ib_vtbl;":""}
${shaderBindings?"static int g_decl_vtbl,g_vertex_shader_vtbl,g_pixel_shader_vtbl,IID_IUnknown,IID_IDirect3DVertexDeclaration9,IID_IDirect3DVertexShader9,IID_IDirect3DPixelShader9;":""}static UINT g_command_count,stream_emissions;
${shaderBindings?"static UINT fvf_creates,fvf_sets;":""}
${stateBindings?"static int g_state_block_vtbl;":""}
static unsigned release_enters,release_exits;
static void trace_enter(const char*m){if(!strcmp(m,"Surface.Release"))release_enters++;}
static void trace_exit(const char*m,HRESULT result,const void*out){(void)result;(void)out;if(!strcmp(m,"Surface.Release"))release_exits++;}
static D9Device*active_device;
static UINT live,allocations,fail_allocation,fail_emit,fail_layout,handles,creates,destroys,device_destroys,checks;
${legacyProgramBindings?"static UINT pair_submissions,pair_fail_submission;":""}
static D9Device*device_from_iface(Interface*p){return (D9Device*)p;}static D9Surface*surface_from_iface(Interface*p){return (D9Surface*)p;}static D9Texture*texture_from_iface(Interface*p){return (D9Texture*)p;}
${textureBindings?"static D9CubeTexture*cube_from_iface(Interface*p){return (D9CubeTexture*)p;}\nstatic D9VolumeTexture*volume_texture_from_iface(Interface*p){return (D9VolumeTexture*)p;}":""}
${legacyFvfBindings?"static UINT active_compatibility_api=9;":""}
${volumeBindings?"static int g_volume_vtbl;static UINT device_compatibility_api(D9Device*d){(void)d;return "+(legacyFvfBindings?"active_compatibility_api":"9")+";}\nstatic D9Volume*volume_from_iface(Interface*p){return (D9Volume*)p;}":""}
static void*GetProcessHeap(void){return NULL;}static void*HeapAlloc(void*h,UINT f,size_t n){(void)h;assert(f==8&&n);if(++allocations==fail_allocation)return NULL;void*p=calloc(1,n);assert(p);live++;return p;}
static void HeapFree(void*h,UINT f,void*p){(void)h;assert(!f);if(p){assert(live);live--;free(p);}}
static void d9wg_vertex_windows_clip_release(void*p,void*context){(void)context;HeapFree(NULL,0,p);}
static void d9wg_clip_buffer_free(void**clips,void(*release)(void*,void*),void*context){if(*clips){void*p=*clips;*clips=NULL;release(p,context);}}
${bufferBindings?"static D9VertexBuffer*vb_from_iface(Interface*p){return (D9VertexBuffer*)p;}\nstatic D9IndexBuffer*ib_from_iface(Interface*p){return (D9IndexBuffer*)p;}":""}
static LONG InterlockedIncrement(LONG*p){return ++*p;}static LONG InterlockedDecrement(LONG*p){assert(*p>0);return --*p;}static LONG InterlockedCompareExchange(LONG*p,LONG v,LONG c){LONG old=*p;if(old==c)*p=v;return old;}
static void decl_destroy_storage(D9VertexDeclaration*);static void shader_destroy_storage(D9Shader*);static void vb_destroy_storage(D9VertexBuffer*);static void vb_binding_add_ref(D9VertexBuffer*);static void vb_binding_release(D9VertexBuffer*);static void ib_destroy_storage(D9IndexBuffer*);static void ib_binding_add_ref(D9IndexBuffer*);static void ib_binding_release(D9IndexBuffer*);static void volume_texture_destroy_storage(D9VolumeTexture*);static void volume_texture_binding_add_ref(D9VolumeTexture*);static void volume_texture_binding_release(D9VolumeTexture*);static void cube_destroy_storage(D9CubeTexture*);static void cube_binding_add_ref(D9CubeTexture*);static void cube_binding_release(D9CubeTexture*);static void texture_destroy_storage(D9Texture*);static void texture_binding_add_ref(D9Texture*);static void texture_binding_release(D9Texture*);static void surface_destroy_storage(D9Surface*);static void surface_binding_release(D9Surface*);
static ULONG device_add_ref(Interface*);static ULONG device_release(Interface*);static ULONG surface_add_ref(Interface*);static ULONG surface_release(Interface*);static ULONG texture_release(Interface*);
#define IDirect3DSurface9_AddRef surface_add_ref
#define IDirect3DSurface9_Release surface_release
#define IDirect3DDevice9_AddRef device_add_ref
#define IDirect3DDevice9_Release device_release
#define IDirect3DTexture9_Release texture_release
${textureBindings?"static ULONG texture_add_ref(Interface*);static ULONG cube_add_ref(Interface*);static ULONG cube_release(Interface*);static ULONG volume_texture_add_ref(Interface*);static ULONG volume_texture_release(Interface*);\nstatic ULONG base_texture_add_ref(Interface*p){return p->lpVtbl==&g_cube_vtbl?cube_add_ref(p):p->lpVtbl==&g_volume_texture_vtbl?volume_texture_add_ref(p):texture_add_ref(p);}\n#define IDirect3DTexture9_AddRef texture_add_ref\n#define IDirect3DBaseTexture9_AddRef base_texture_add_ref":"#define IDirect3DTexture9_AddRef(p) (assert(0),(void)(p),0u)"}
${textureBindings?"#define IDirect3DCubeTexture9_AddRef cube_add_ref\n#define IDirect3DCubeTexture9_Release cube_release":"#define IDirect3DCubeTexture9_AddRef(p) (assert(0),(void)(p),0u)\n#define IDirect3DCubeTexture9_Release(p) (assert(0),(void)(p),0u)"}
#define IDirect3D9_Release(p) (--((Parent*)(p))->refs)
${bufferBindings?"static ULONG vb_add_ref(Interface*);static ULONG vb_release(Interface*);static ULONG ib_add_ref(Interface*);static ULONG ib_release(Interface*);\n#define IDirect3DVertexBuffer9_AddRef vb_add_ref\n#define IDirect3DVertexBuffer9_Release vb_release\n#define IDirect3DIndexBuffer9_AddRef ib_add_ref\n#define IDirect3DIndexBuffer9_Release ib_release":"#define IDirect3DVertexBuffer9_Release(p) ((void)(p),assert(0),0u)\n#define IDirect3DIndexBuffer9_Release(p) ((void)(p),assert(0),0u)"}
${textureBindings?"#define IDirect3DVolumeTexture9_AddRef volume_texture_add_ref\n#define IDirect3DVolumeTexture9_Release volume_texture_release":"#define IDirect3DVolumeTexture9_AddRef(p) ((void)(p),assert(0),0u)\n#define IDirect3DVolumeTexture9_Release(p) ((void)(p),assert(0),0u)"}
${volumeBindings?"static ULONG volume_add_ref(Interface*);\n#define IDirect3DVolume9_AddRef volume_add_ref":""}
${shaderBindings?`static ULONG decl_add_ref(Interface*);static ULONG decl_release(Interface*);static ULONG vertex_shader_add_ref(Interface*);static ULONG vertex_shader_release(Interface*);static ULONG pixel_shader_add_ref(Interface*);static ULONG pixel_shader_release(Interface*);
#define IDirect3DVertexDeclaration9_AddRef decl_add_ref
#define IDirect3DVertexDeclaration9_Release decl_release
#define IDirect3DVertexShader9_AddRef vertex_shader_add_ref
#define IDirect3DVertexShader9_Release vertex_shader_release
#define IDirect3DPixelShader9_AddRef pixel_shader_add_ref
#define IDirect3DPixelShader9_Release pixel_shader_release
static D9VertexDeclaration*decl_from_iface(Interface*p){return (D9VertexDeclaration*)p;}
static D9Shader*vertex_shader_from_iface(Interface*p){return (D9Shader*)p;}
static D9Shader*pixel_shader_from_iface(Interface*p){return (D9Shader*)p;}
static BOOL guid_equal(REFIID a,REFIID b){return a==b;}
static BOOL iid_is_unknown(REFIID p){return p==&IID_IUnknown;}
static BOOL fvf_to_declaration(DWORD f,D9WGVertexElement*w,UINT*n){${fvfBindings?"return d9wg_fvf_decode(9,f,w,18,n,NULL);":"*w=(D9WGVertexElement){0,0,2,0,0,0};*n=1;return f==2;"}}
static BOOL emit_set_fvf(D9Device*d,DWORD f,const D9WGVertexElement*w,UINT n){(void)d;(void)w;assert(${fvfBindings?"f&&n&&n<=18":"f==2&&n==1"});fvf_sets++;return !fail_emit;}
static BOOL emit_vertex_declaration_create(D9Device*d,UINT h,const D9WGVertexElement*w,UINT n){(void)w;assert(d==active_device&&h&&n<=18);if(fail_emit)return FALSE;fvf_creates++;return TRUE;}
`:""}
${upBindings?`static UINT up_emits,up_invalidations,up_status_calls;static HRESULT up_status_result;
static HRESULT d9_prepare_clip_status(D9Device*d,const D9WGClipDrawSource*s,BOOL bound,D9WGClipStatus*next,BOOL*valid){(void)d;assert(s->vertices&&s->stride&&!bound);up_status_calls++;*next=(D9WGClipStatus){17,19};*valid=TRUE;return up_status_result;}
static BOOL emit_draw_primitive_up(const D9WGDrawPrimitiveUP*c,const void*p){assert(c->vertex_bytes&&p);up_emits++;return !fail_emit;}
static BOOL emit_draw_indexed_primitive_up(const D9WGDrawIndexedPrimitiveUP*c,const void*i,const void*v){assert(c->vertex_bytes&&c->index_bytes&&i&&v);up_emits++;return !fail_emit;}
static void invalidate_render_target_mirrors(D9Device*d){(void)d;up_invalidations++;}
`:""}
static void device_release_owned_references(D9Device*);
${stateBindings?"static void device_clear_state_block_handles8(D9Device*);":"static void device_clear_state_block_handles8(D9Device*d){(void)d;}"}
${stateBindings?"static ULONG state_block_release(Interface*);\n#define IDirect3DStateBlock9_Release state_block_release\n"+extract(source,"device_cancel_recording"):"static void device_cancel_recording(D9Device*d){(void)d;}"}
static void SetRect(RECT*r,int l,int t,int right,int b){*r=(RECT){l,t,right,b};}
static HRESULT IDirect3DDevice9_SetViewport(Interface*p,const D3DVIEWPORT9*v){(void)p;assert(v->Width==32&&(v->Height==24${cubeBindings?"||v->Height==32":""}));return 0;}
static HRESULT IDirect3DDevice9_SetScissorRect(Interface*p,const RECT*r){(void)p;assert(r->right==32&&(r->bottom==24${cubeBindings?"||r->bottom==32":""}));return 0;}
static HRESULT IDirect3DDevice9_GetBackBuffer(Interface*p,UINT a,UINT b,UINT c,Interface**out){(void)p;(void)a;(void)b;(void)c;(void)out;assert(0);return -1;}
static void swap_chain_destroy_storage(D9SwapChain*c){(void)c;assert(0);}
static void d9wg_light_free(void*p){(void)p;}static void d9wg_palette_free(void*p){(void)p;}static void unwatch_window_state(D9Device*d){(void)d;}static void restore_display_mode(D9Device*d){(void)d;}
static void SelectObject(void*a,void*b){(void)a;(void)b;}static void DeleteDC(void*p){(void)p;}static void DeleteObject(void*p){(void)p;}
static void EnterCriticalSection(int*p){(void)p;${viewBindings?"view_lock_depth++;":""}}static void LeaveCriticalSection(int*p){(void)p;${viewBindings?"assert(view_lock_depth);view_lock_depth--;":""}}static BOOL submit_batch_locked(BOOL present){assert(!present);return 1;}
static UINT allocate_handle(void){return ++handles;}
static BOOL texture_level_layout(D3DFORMAT f,UINT w,UINT h,UINT*p,UINT*r,UINT*b){if(fail_layout||f!=22)return 0;*p=w*4;*r=h;*b=w*h*4;return 1;}
static BOOL emit_texture_create(D9Device*d,D9Texture*t){assert(t->device==d&&t->width&&t->height&&t->level_count==1&&t->multisample_quality==0);creates++;return !fail_emit;}
${shaderStorageBindings?`static BOOL shader_storage_retiring;
static BOOL shader_storage_owned_only(D9Device*d){for(D9VertexDeclaration*x=d->vertex_declarations;x;x=x->next_device_resource)if(x->refcount||x->binding_refs!=1||x->fvf_cache_ref)return FALSE;for(D9Shader*x=d->shaders;x;x=x->next_device_resource)if(x->refcount||x->binding_refs!=1)return FALSE;return TRUE;}
`:""}
static BOOL emit_command(UINT op,const void*p,UINT n){assert(active_device&&(op==D9WG_OP_DESTROY_RESOURCE&&((const D9WGDestroyResource*)p)->resource_kind==0?active_device->refcount==0:active_device->refcount>0${shaderStorageBindings?"||(shader_storage_retiring&&op==D9WG_OP_DESTROY_RESOURCE&&((const D9WGDestroyResource*)p)->resource_kind>=6&&((const D9WGDestroyResource*)p)->resource_kind<=8)":""}));if(op==D9WG_OP_SET_VERTEX_DECLARATION||op==D9WG_OP_SET_VERTEX_SHADER||op==D9WG_OP_SET_PIXEL_SHADER){${legacyProgramBindings?"pair_submissions++;if(pair_submissions==pair_fail_submission)return FALSE;":""}return !fail_emit;}if(op==D9WG_OP_SET_STREAM_SOURCE){stream_emissions++;assert(n==sizeof(D9WGSetStreamSource));return !fail_emit;}if(op==D9WG_OP_SET_INDICES){assert(n==sizeof(D9WGSetIndices));return !fail_emit;}if(op==D9WG_OP_SET_TEXTURE){assert(n==sizeof(D9WGSetTexture));return !fail_emit;}if(op==D9WG_OP_SET_RENDER_TARGET){assert(n==sizeof(D9WGSetRenderTarget));return 1;}if(op==D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL){assert(n==sizeof(D9WGSetDepthStencilSurfaceLevel));return 1;}assert(op==D9WG_OP_DESTROY_RESOURCE&&n==sizeof(D9WGDestroyResource));const D9WGDestroyResource*c=p;if(c->resource_kind>=1&&c->resource_kind<=8)destroys++;else{assert(!c->resource_kind&&c->resource_handle==7&&!active_device->vertex_buffers&&!active_device->index_buffers&&!active_device->texture_resources&&!active_device->cube_textures&&!active_device->volume_textures&&!active_device->depth_stencil_surface&&!active_device->child_parent_refs${shaderStorageBindings?"&&shader_storage_owned_only(active_device)":"&&!active_device->vertex_declarations&&!active_device->shaders"}&&!active_device->state_blocks);for(UINT i=0;i<4;i++)assert(!active_device->render_target_surfaces[i]);device_destroys++;}return 1;}

${stateBindings?`static D9StateBlock*state_block_from_iface(Interface*p){return (D9StateBlock*)p;}
static UINT d9wg_state_block_transform_state(UINT i){return i;}
static D9WGLightState*d9wg_light_find(void*p,UINT i){(void)p;(void)i;return NULL;}
#define IDirect3DDevice9_SetRenderState(...) ((void)0)
#define IDirect3DDevice9_SetTextureStageState(...) ((void)0)
#define IDirect3DDevice9_SetSamplerState(...) ((void)0)
#define IDirect3DDevice9_SetTransform(...) ((void)0)
#define IDirect3DDevice9_SetMaterial(...) ((void)0)
#define IDirect3DDevice9_SetClipPlane(...) ((void)0)
#define IDirect3DDevice9_SetNPatchMode(...) ((void)0)
#define IDirect3DDevice9_SetLight(...) ((void)0)
#define IDirect3DDevice9_LightEnable(...) ((void)0)
#define IDirect3DDevice9_SetVertexShaderConstantF(...) ((void)0)
#define IDirect3DDevice9_SetPixelShaderConstantF(...) ((void)0)
#define IDirect3DDevice9_SetVertexShaderConstantI(...) ((void)0)
#define IDirect3DDevice9_SetPixelShaderConstantI(...) ((void)0)
#define IDirect3DDevice9_SetVertexShaderConstantB(...) ((void)0)
#define IDirect3DDevice9_SetPixelShaderConstantB(...) ((void)0)
#define IDirect3DDevice9_SetStreamSourceFreq(...) ((void)0)
#define IDirect3DDevice9_SetTexture device_set_texture
#define IDirect3DDevice9_SetVertexShader device_set_vertex_shader
#define IDirect3DDevice9_SetPixelShader device_set_pixel_shader
#define IDirect3DDevice9_SetVertexDeclaration device_set_vertex_declaration
#define IDirect3DDevice9_SetFVF device_set_fvf
#define IDirect3DDevice9_SetStreamSource device_set_stream_source
#define IDirect3DDevice9_SetIndices device_set_indices
static UINT texture_binding_stage(UINT i){return i<16?i:i+241;}
static HRESULT device_set_current_texture_palette(Interface*p,UINT n){((D9Device*)p)->current_palette=n;return 0;}
`:""}
${viewBindings?['core_view_device8','core_view_link8','d9wg_d3d9_core_get_view8',...(source.includes('static HRESULT core_view_attach8(')?['core_view_attach8']:[]),'d9wg_d3d9_core_attach_view8','core_view_retire8',...(source.includes('static void core_device_cleanup8(')?['core_device_cleanup8']:[])].map(n=>extract(source,n)).join('\n')+(source.includes('static void core_device_cleanup8(')?'':'\nstatic void core_device_cleanup8(D9Device*d){(void)d;}'):'static void core_device_cleanup8(D9Device*d){(void)d;}'}
${["decl_destroy_storage", "decl_binding_add_ref", "decl_binding_release", "device_clear_fvf_cache", "shader_destroy_storage", "shader_binding_add_ref", "shader_binding_release", "device_add_ref", "device_child_add_ref", "device_child_release", "device_release", "surface_is_implicit", "surface_uses_private_bindings", "reset_buffer_owned_refs", "reset_bound_surface", "reset_surface_owned_refs", "reset_texture_owned_refs", "device_has_reset_blockers", "device_clear_bindings", "device_release_owned_references", "vb_destroy_storage", "vb_binding_add_ref", "vb_binding_release", "ib_destroy_storage", "ib_binding_add_ref", "ib_binding_release", "volume_texture_destroy_storage", "volume_texture_binding_add_ref", "volume_texture_binding_release", "cube_destroy_storage", "cube_binding_add_ref", "cube_binding_release", "texture_destroy_storage", "texture_binding_add_ref", "texture_binding_release", "texture_release", "surface_binding_add_ref", "surface_binding_release", "surface_destroy_storage", "surface_add_ref", "surface_release", "create_target_texture", "surface_target_handle", ...(source.includes("static void device_replace_render_target_surface(")?["device_replace_render_target_surface"]:[]), "device_set_render_target", "device_get_render_target", ...(source.includes("static void device_replace_depth_stencil_surface(")?["device_replace_depth_stencil_surface"]:[]), "device_set_depth_stencil_surface", "device_get_depth_stencil_surface", ...(declarationBindings?["declaration_element_supported","parse_vertex_declaration","declaration_fvf","device_create_vertex_declaration"]:[]), ...(shaderBindings?["device_fvf_declaration","decl_add_ref","decl_release","decl_query_interface","decl_get_device","decl_get_declaration","shader_public_add_ref","shader_public_release","shader_query_interface","shader_get_function","vertex_shader_add_ref","vertex_shader_release","vertex_shader_query_interface","vertex_shader_get_device","vertex_shader_get_function","pixel_shader_add_ref","pixel_shader_release","pixel_shader_query_interface","pixel_shader_get_device","pixel_shader_get_function","emit_set_shader","device_set_vertex_declaration","device_get_vertex_declaration","device_set_fvf","device_get_fvf","device_set_vertex_shader","device_get_vertex_shader","device_set_pixel_shader","device_get_pixel_shader"]:[]), ...(textureBindings?["texture_binding_slot","texture_add_ref","cube_add_ref","cube_release","volume_texture_add_ref","volume_texture_release","texture_get_surface_level","device_set_texture","device_get_texture",...(cubeBindings?["cube_get_cube_map_surface"]:[]),...(volumeBindings?["volume_add_ref","volume_release","volume_texture_get_volume_level"]:[]),...(bufferBindings?["vb_add_ref","vb_release","ib_add_ref","ib_release","device_set_stream_binding","device_set_stream_source","device_get_stream_source","device_set_indices","device_get_indices","clear_stream_zero_after_up","clear_indices_after_indexed_up"]:[])]:[]), ...(rootBindings?["texture_get_device","cube_get_device","volume_texture_get_device","vb_get_device","ib_get_device"]:[]), ...(stateBindings?["device_clear_state_block_handles8","state_block_release_references","state_block_capture","state_block_apply","state_block_alloc","state_block_release","state_block_capture_method","state_block_apply_method","device_begin_state_block","device_end_state_block"]:[]), ...(upBindings?["multiply_u32","primitive_element_count","device_has_vertex_format","clear_bindings_after_up_missing_format","device_draw_primitive_up","draw_indexed_up","device_draw_indexed_primitive_up"]:[])].map(n => extract(source, n)).join("\n")}
${tokenBindings?'#define IDirect3DStateBlock9_AddRef state_block_add_ref\n#define IDirect3DStateBlock9_Apply state_block_apply_method\n'+extract(source,"state_block_add_ref"):""}
${legacyFvfBindings?'#define IDirect3DDevice9_CreateVertexDeclaration device_create_vertex_declaration\n'+(snapshotBindings?'#define IDirect3DDevice9_BeginStateBlock device_begin_state_block\n#define IDirect3DDevice9_EndStateBlock device_end_state_block\n':'')+['fvf8_declaration','d9wg_d3d9_core_set_fvf8',...(legacyProgramBindings?['d9wg_d3d9_core_set_vertex_shader8']:[]),...(snapshotBindings?['d9wg_d3d9_core_record_state_block8']:[]),...(tokenBindings?['state_block_handle8','state_block_adopt_handle8','state_block_publish_handle8','d9wg_d3d9_core_create_state_block_handle8','d9wg_d3d9_core_apply_state_block_handle8','d9wg_d3d9_core_capture_state_block_handle8','d9wg_d3d9_core_delete_state_block_handle8']:[]),...(incrementalBindings?['d9wg_d3d9_core_begin_state_block8','d9wg_d3d9_core_end_state_block_handle8']:[])].map(n=>extract(source,n)).join('\n'):''}
#define CHECK(v) do{if(!(v)||release_enters!=release_exits){fprintf(stderr,"Target binding line %u: %s release markers=%u/%u\\n",__LINE__,#v,release_enters,release_exits);return 1;}checks++;}while(0)
static D9Device*new_device(Parent*p){D9Device*d=HeapAlloc(NULL,8,sizeof(*d));d->refcount=1;${viewBindings?"d->iface.lpVtbl=&g_device_vtbl;":""}d->handle=7;d->parent=p;p->refs=1;active_device=d;return d;}
static HRESULT get_bound(D9Device*d,BOOL depth,Interface**out){return depth?device_get_depth_stencil_surface(&d->iface,out):device_get_render_target(&d->iface,3,out);}
static HRESULT bind_target(D9Device*d,BOOL depth,D9Surface*s){return depth?device_set_depth_stencil_surface(&d->iface,s?&s->iface:NULL):device_set_render_target(&d->iface,3,s?&s->iface:NULL);}
${textureBindings?'#include "tests/native/d9wg_texture_binding_cases.h"':""}
${cubeBindings?'#include "tests/native/d9wg_cube_binding_cases.h"':""}
${volumeBindings?'#include "tests/native/d9wg_volume_binding_cases.h"':""}
${bufferBindings?'#include "tests/native/d9wg_buffer_binding_cases.h"':""}
${shaderBindings?'#include "tests/native/d9wg_shader_binding_cases.h"':""}
${stateBindings?'#include "tests/native/d9wg_state_binding_cases.h"':""}
${rootBindings?'#include "tests/native/d9wg_root_binding_cases.h"':""}
${streamBindings?'#include "tests/native/d9wg_stream_binding_cases.h"':""}
${fvfBindings?'#include "tests/native/d9wg_fvf_binding_cases.h"':""}
${declarationBindings?'#include "tests/native/d9wg_declaration_fvf_cases.h"':""}
${upBindings?'#include "tests/native/d9wg_up_failure_cases.h"':""}
${zeroUp?'#include "tests/native/d9wg_zero_up_cases.h"':""}
${legacyFvfBindings?'#include "tests/native/d8wg_fvf_core_cases.h"':""}
${legacyProgramBindings?'#include "tests/native/d8wg_program_binding_core_cases.h"':""}
${snapshotBindings?'#include "tests/native/d8wg_snapshot_block_core_cases.h"':""}
${tokenBindings?'#include "tests/native/d8wg_state_block_handle_core_cases.h"':""}
${incrementalBindings?'#include "tests/native/d8wg_incremental_block_core_cases.h"':""}
${viewBindings?'#include "tests/native/d8wg_object_view_core_cases.h"':""}
${deviceViewBindings?'#include "tests/native/d8wg_device_view_core_cases.h"':""}
${deviceCleanupBindings?["d9wg_d3d9_core_attach_device_view8","d9wg_d3d9_core_release_owned8"].map(n=>extract(source,n)).join("\n"):""}
${shaderStorageBindings?["shader_storage_reference8","d9wg_d3d9_core_shader_storage_add_ref8","d9wg_d3d9_core_shader_storage_release8"].map(n=>extract(source,n)).join("\n")+'\n#include "tests/native/d8wg_shader_storage_core_cases.h"':""}
${deviceCleanupBindings?'#include "tests/native/d8wg_device_cleanup_core_cases.h"':""}
int main(void){${upBindings?"(void)&clear_bindings_after_up_missing_format;":""}${declarationBindings?"(void)&declaration_fvf;":""}(void)&device_clear_fvf_cache;(void)&core_device_cleanup8;${shaderBindings?"(void)&device_fvf_declaration;":""}${bufferBindings?"(void)&device_set_stream_binding;":""}${textureBindings?"(void)&volume_texture_release;":""}(void)&decl_binding_add_ref;(void)&decl_binding_release;(void)&shader_binding_add_ref;(void)&shader_binding_release;(void)&vb_binding_add_ref;(void)&vb_binding_release;(void)&ib_binding_add_ref;(void)&ib_binding_release;(void)&volume_texture_binding_add_ref;(void)&volume_texture_binding_release;(void)&cube_binding_add_ref;(void)&cube_binding_release;(void)&texture_binding_add_ref;(void)&texture_binding_release;${textureBindings?"CHECK(!texture_binding_cases());":""}${cubeBindings?"CHECK(!cube_binding_cases());":""}${volumeBindings?"CHECK(!volume_binding_cases());":""}${bufferBindings?"CHECK(!buffer_binding_cases());":""}${shaderBindings?"CHECK(!shader_binding_cases());":""}${stateBindings?"CHECK(!state_binding_cases());":""}${rootBindings?"CHECK(!root_binding_cases());":""}${streamBindings?"CHECK(!stream_binding_cases());":""}${fvfBindings?"CHECK(!fvf_binding_cases());":""}${declarationBindings?"CHECK(!declaration_fvf_cases());":""}${upBindings?"CHECK(!up_failure_cases());":""}${zeroUp?"CHECK(!zero_up_cases());":""}${legacyFvfBindings?"CHECK(!fvf8_core_cases());":""}${legacyProgramBindings?"CHECK(!program8_binding_cases());":""}${snapshotBindings?"CHECK(!snapshot8_block_cases());":""}${tokenBindings?"CHECK(!token8_block_cases());":""}${incrementalBindings?"CHECK(!incremental8_block_cases());":""}${viewBindings?"CHECK(!view8_core_cases());":""}${deviceViewBindings?"CHECK(!device_view8_core_cases());":""}${shaderStorageBindings?"CHECK(!shader_storage8_core_cases());":""}${deviceCleanupBindings?"CHECK(!device_cleanup8_core_cases());":""}(void)surface_destroy_storage;(void)surface_binding_add_ref;(void)surface_binding_release;unsigned cases=0,revivals=0;for(UINT profile=0;profile<4;profile++)for(UINT order=0;order<4;order++){
 Parent parent={0};D9Device*d=new_device(&parent);BOOL depth=profile>=2;UINT bindings=depth?1:order%2?1:2;Interface*out=NULL;UINT old_destroys=destroys;
 CHECK(!create_target_texture(d,32,24,depth?75:22,depth?2:1,profile%2?4:0,0,&out)&&out);D9Surface*s=(D9Surface*)out;
 CHECK(!bind_target(d,depth,s));if(bindings==2)CHECK(!device_set_render_target(&d->iface,2,out));
 CHECK(s->refcount==1&&s->binding_refs==(LONG)bindings&&d->refcount==2&&d->child_parent_refs==1&&device_has_reset_blockers(d));
 CHECK(!surface_release(out)&&s->refcount==0&&d->refcount==1&&!d->child_parent_refs&&s->texture->refcount==1&&!device_has_reset_blockers(d));
 CHECK(!surface_release(out)&&s->refcount==0&&d->refcount==1&&destroys==old_destroys);
 for(UINT cycle=0;cycle<64;cycle++){
  Interface*q=NULL;CHECK(!get_bound(d,depth,&q)&&q==out&&s->refcount==1&&d->refcount==2&&d->child_parent_refs==1);
  CHECK(surface_add_ref(q)==2&&d->refcount==2&&surface_release(q)==1&&d->refcount==2);
  CHECK(!surface_release(q)&&s->refcount==0&&d->refcount==1&&!d->child_parent_refs&&destroys==old_destroys);
  CHECK(!bind_target(d,depth,s)&&s->refcount==0&&s->binding_refs==(LONG)bindings&&d->refcount==1&&!device_has_reset_blockers(d));revivals++;
 }
 s->texture->levels[0].locked=1;CHECK(device_has_reset_blockers(d));s->texture->levels[0].locked=0;CHECK(!device_has_reset_blockers(d));
 if(order==0){if(bindings==2)CHECK(!device_set_render_target(&d->iface,2,NULL)&&s->binding_refs==1);CHECK(!bind_target(d,depth,NULL)&&destroys==old_destroys+1&&d->refcount==1&&!d->texture_resources&&live==1);CHECK(!device_release(&d->iface)&&!live&&!parent.refs);}
 if(order==1){CHECK(!device_release(&d->iface)&&!live&&!parent.refs&&destroys==old_destroys+1);}
 if(order==2){Interface*q=NULL;CHECK(!get_bound(d,depth,&q)&&q==out&&d->refcount==2);device_clear_bindings(d);CHECK(s->refcount==1&&!s->binding_refs&&d->refcount==2&&destroys==old_destroys);CHECK(device_release(&d->iface)==1&&d->child_parent_refs==1);CHECK(!surface_release(q)&&!live&&!parent.refs&&destroys==old_destroys+1);}
 if(order==3){device_clear_bindings(d);CHECK(d->refcount==1&&!d->texture_resources&&destroys==old_destroys+1&&live==1);CHECK(!device_release(&d->iface)&&!live&&!parent.refs);}
 cases++;
}
D9Surface s={0};D9Texture t={0};D9SwapChain chain={0};CHECK(surface_uses_private_bindings(&s));s.texture_child=1;CHECK(!surface_uses_private_bindings(&s));s.texture_child=0;s.cube_texture=&t;CHECK(!surface_uses_private_bindings(&s));s.cube_texture=NULL;s.swap_chain=&chain;CHECK(surface_uses_private_bindings(&s));chain.handle=1;CHECK(surface_uses_private_bindings(&s));s.swap_chain=NULL;s.auto_depth_stencil=1;CHECK(surface_uses_private_bindings(&s));
printf("PASS actual D3D9 target bindings: %u checks, %u lifecycle groups, %u public-zero revivals; actual RT/depth setters/getters, one/two internal slots, one public device pin, public-only Reset blockers, same-object rebinding and unbind/device-first/last-public cleanup\\n",checks,cases,revivals);return 0;}

`);
    const b = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", "-I", root, dir + "/target.c", root + "/runtime/d9wg_private_data.c", ...(fvfBindings?[process.env.D8WG_FVF8_DECODE_SOURCE||root + "/runtime/d9wg_fvf.c"]:[]), "-o", dir + "/target"], {encoding: "utf8"}); assert.equal(b.status, 0, b.stderr);
    const r = spawnSync(dir + "/target", [], {encoding: "utf8", timeout: legacyFvfBindings?30000:10000}); assert.equal(r.status, 0, r.stderr + " signal=" + r.signal); process.stdout.write(r.stdout);
} finally {fs.rmSync(dir, {recursive: true, force: true});}
