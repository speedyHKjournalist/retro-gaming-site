"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.D9WG_BOUND_DRAW_SOURCE||root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-bound-draw-"));
function extract(name){const m=new RegExp((name.startsWith("d9wg_d3d9_core_")?"HRESULT WINAPI ":"static (?:BOOL|void|int|HRESULT)(?: WINAPI)? ")+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return source.slice(m.index,at);}
const processVertices8 = process.env.D9WG_TEST_PROCESS8 === "1";
const legacyIndexed = process.env.D9WG_TEST_INDEXED8 === "1";
const input=/typedef struct D9ProcessInput \{[\s\S]*?\} D9ProcessInput;/.exec(source);assert(input);
try{
 const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_fvf.h"
#include "d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD;typedef uint8_t BYTE;typedef int BOOL,HRESULT,INT,D3DPRIMITIVETYPE;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDCALL (-1)
#define E_OUTOFMEMORY (-2)
#define D3DERR_DRIVERINTERNALERROR (-3)
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define TRACE_REFUSE(x) (x)
#define TRACE_FRAME_REJECT() ((void)0)
#define TRACE_FRAME_DRAW(n) ((void)(n))
#define D9_MAX_STREAMS 16
#define D3DMAXDECLLENGTH 64
#define D9_STREAMSOURCE_FREQUENCY_MASK 0x3fffffffu
#define D3DSTREAMSOURCE_INDEXEDDATA 0x40000000u
#define D3DSTREAMSOURCE_INSTANCEDATA 0x80000000u
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x20u
#define D3DCREATE_MIXED_VERTEXPROCESSING 0x80u
#define D3DRS_CLIPPING 136
#define D3DRS_CLIPPLANEENABLE 152
#define D3DRS_SHADEMODE 9
#define D3DSHADE_FLAT 1
#define D3DTS_WORLD 10
#define D3DTS_VIEW 0
#define D3DTS_PROJECTION 1
#define D3DFMT_INDEX16 101
#define D3DPT_POINTLIST 1
#define D3DPT_LINELIST 2
#define D3DPT_LINESTRIP 3
#define D3DPT_TRIANGLELIST 4
#define D3DPT_TRIANGLESTRIP 5
#define D3DPT_TRIANGLEFAN 6
#define HEAP_ZERO_MEMORY 8
#define GetProcessHeap() NULL
#define HeapAlloc(h,f,s) ((void)(h),(f)?calloc(1,s):malloc(s))
#define HeapFree(h,f,p) ((void)(h),(void)(f),free(p),1)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
typedef D9WGProcessLayout D9ProcessLayout;
#include "runtime/d9wg_vertex_windows.h"
typedef struct D9Device D9Device;typedef D9Device IDirect3DDevice9;
${processVertices8?`typedef struct{const void*lpVtbl;}ProcessInterface;
typedef ProcessInterface IDirect3DVertexBuffer9,IDirect3DVertexDeclaration9;
static int g_vb_vtbl;
typedef struct{float values[16];}D3DMATRIX;
typedef struct{float GuardBandLeft,GuardBandTop,GuardBandRight,GuardBandBottom;}D3DCAPS9;
#define D3DPV_DONOTCOPYDATA 1u
#define D3DTS_TEXTURE0 2
#define D3DFVF_POSITION_MASK 0x000eu
#define D3DFVF_XYZ 2u
#define D3DFVF_XYZRHW 4u
#define D3DFVF_NORMAL 0x10u
#define HOSTLOG_REFUSED(...)
#define TRACE(...)
#define TRACE_MARK_ENTER(...)
#define TRACE_MARK_EXIT(...)
#include "runtime/d9wg_math.h"
#include "${process.env.D8WG_PROCESS_LAYOUT8_HEADER||root+'/runtime/d9wg_process_layout8.h'}"
static UINT process_attempts,process_live,process_fail_at,process_allocations,process_frees;
static BOOL process_lighting_failure;
static void*process_alloc(UINT flags,size_t n){if(++process_attempts==process_fail_at)return NULL;void*p=flags?calloc(1,n):malloc(n);if(p){process_live++;process_allocations++;}return p;}
static void process_free(void*p){if(p){if(!process_live)abort();process_live--;process_frees++;free(p);}}
#undef HeapAlloc
#undef HeapFree
#define HeapAlloc(h,f,n) ((void)(h),process_alloc(f,n))
#define HeapFree(h,f,p) ((void)(h),(void)(f),process_free(p),1)
`:""}
typedef struct D9VertexBuffer{${processVertices8?"ProcessInterface iface;DWORD fvf;":""}D9Device *device;BYTE *shadow;UINT length,handle,lock_offset,lock_size,lock_flags;BOOL locked;D9WGClipBuffer clips;}D9VertexBuffer;
typedef struct D9IndexBuffer{D9Device *device;BYTE *shadow;UINT length,format,handle,lock_offset,lock_size,lock_flags;BOOL locked;}D9IndexBuffer;
typedef struct D9VertexDeclaration{${processVertices8?"ProcessInterface iface;D9Device*device;":""}UINT element_count;struct{UINT Stream,Offset,Type,Method,Usage,UsageIndex;}elements[64];}D9VertexDeclaration;
struct D9Device{D9VertexDeclaration *vertex_declaration;DWORD fvf;void *vertex_shader;struct{D9VertexBuffer *buffer;UINT offset,stride,frequency;}streams[16];D9IndexBuffer *index_buffer;UINT handle;float npatch_segments;D9WGClipStatus clip_status;BOOL clip_status_valid,software_vertex_processing;struct{DWORD BehaviorFlags;}creation;DWORD render_states[256];${processVertices8?"DWORD texture_stage_states[8][33];":""}float transforms[266][16],clip_planes[6][4];struct{UINT X,Y,Width,Height;float MinZ,MaxZ;}viewport;};
static BOOL flush_managed_textures(D9Device*d){(void)d;return TRUE;}
static unsigned uploads,upload_fail_at,sequence_count;static char sequence[128];
static unsigned emits,invalidations;static int transport_ok=1;static UINT last_opcode;static D9WGDrawIndexedPrimitive indexed;
static D9Device *device_from_iface(IDirect3DDevice9 *d){return d;}
${legacyIndexed||processVertices8?"static UINT active_api=8;static UINT device_compatibility_api(D9Device*d){(void)d;return active_api;}":""}
static BOOL emit_command(UINT op,const void *data,UINT bytes){if(op==D9WG_OP_DRAW_PRIMITIVE||op==D9WG_OP_DRAW_INDEXED_PRIMITIVE)sequence[sequence_count++]='D';++emits;last_opcode=op;if(op==D9WG_OP_DRAW_INDEXED_PRIMITIVE){if(bytes!=sizeof(indexed))abort();memcpy(&indexed,data,bytes);}return transport_ok;}
static BOOL emit_draw_primitive_up(const D9WGDrawPrimitiveUP *p,const void*v){(void)p;(void)v;abort();}
static void invalidate_render_target_mirrors(D9Device *d){(void)d;++invalidations;}
static BOOL fvf_to_declaration(DWORD fvf,D9WGVertexElement *e,UINT *count){return d9wg_fvf_decode(9,fvf,e,64,count,NULL);}
static BOOL d9_build_process_layout(const D9WGVertexElement *e,UINT n,D9ProcessLayout*l){return d9wg_process_layout(e,n,l);}
static int d9_clip_stream_draw(D9Device*d,uint32_t t,const D9WGClipDrawSource*s){(void)d;(void)t;(void)s;return 0;}

${processVertices8?"static UINT process_upload_handle,process_upload_offset,process_upload_size,process_upload_flags;static BYTE process_upload_bytes[8192];":""}
static BOOL emit_buffer_update(uint32_t h,uint32_t off,const void *p,uint32_t n,uint32_t f){(void)h;(void)off;(void)p;(void)n;(void)f;${processVertices8?'process_upload_handle=h;process_upload_offset=off;process_upload_size=n;process_upload_flags=f;if(n>sizeof(process_upload_bytes)||!p)abort();memcpy(process_upload_bytes,p,n);':''}sequence[sequence_count++]='U';uploads++;return transport_ok&&uploads!=upload_fail_at;}
#define D9WG_MAPPED_DEVICE D9Device
#define D9WG_MAPPED_VERTEX D9VertexBuffer
#define D9WG_MAPPED_INDEX D9IndexBuffer
#include "runtime/d9wg_mapped_draw_bridge.h"
${input[0]}
static HRESULT d9_draw_npatch(IDirect3DDevice9*i,D9Device*d,D3DPRIMITIVETYPE t,UINT c,const void*p,UINT s,UINT b){(void)i;(void)d;(void)t;(void)c;(void)p;(void)s;(void)b;abort();}
${["multiply_u32","primitive_element_count","device_has_vertex_format","d9_current_source_elements","d9_current_source_layout","d9_current_source_position_transformed","d9_process_input_free","d9_process_input","d9_prepare_stream_clip_status","d9_draw_index_bounds","d9_bound_draw_range","d9_clip_emit","d9_clip_mask","d9_clip_vb_draw","d9_software_vertex_processing","d9_prepare_clip_status","device_draw_primitive","device_draw_indexed_primitive"].map(extract).join("\n")}
${processVertices8?`
static D9VertexBuffer*vb_from_iface(IDirect3DVertexBuffer9*p){return (D9VertexBuffer*)p;}
static D9VertexDeclaration*decl_from_iface(IDirect3DVertexDeclaration9*p){return (D9VertexDeclaration*)p;}
static void fill_caps(D3DCAPS9*c){*c=(D3DCAPS9){-32768,-32768,32768,32768};}
static BOOL d9_prepare_lighting(D9Device*d,const D3DMATRIX*m,D9WGTNLLighting*l){(void)d;(void)m;memset(l,0,sizeof(*l));return !process_lighting_failure;}
`+["d9_multiply_matrix","process_vertices_layout","device_process_vertices","d9wg_d3d9_core_process_vertices8"].map(extract).join("\n")+`
#define device_process_vertices oracle_process_vertices
#include "tests/fixtures/d3d9_process_vertices_before_core.c"
#undef device_process_vertices
typedef D9WGProcessLayout D8FVFLayout;
#define decode_fvf_layout oracle_decode_fvf_layout
#include "tests/fixtures/d3d8_process_layout_before_core.c"
#undef decode_fvf_layout
`:""}
${legacyIndexed?"#define IDirect3DDevice9_DrawIndexedPrimitive device_draw_indexed_primitive\n"+extract("d9wg_d3d9_core_draw_indexed8"):""}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"D3D9 bound draw line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
${legacyIndexed||processVertices8?"#define main retained_bound_draw_main":""}
#include "tests/native/d9wg_bound_draw_cases.h"
${legacyIndexed?'#undef main\n#include "tests/native/d8wg_indexed_core_cases.h"':""}
${processVertices8?'#undef main\n#include "tests/native/d8wg_process_core_cases.h"':""}
`;
 fs.writeFileSync(dir+"/windows.h","/* allocation supplied by native harness */\n");fs.writeFileSync(dir+"/draw.c",code);
 const sources=["clip","clip_draw","blend","vertex","fvf","math","lighting","tnl_math","buffer_lock"].map(n=>root+"/runtime/d9wg_"+n+".c");
 const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-Wno-unused-value","-I",dir,"-I",root,"-I",root+"/runtime",dir+"/draw.c",...sources,"-o",dir+"/draw"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);
 const run=spawnSync(dir+"/draw",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||"native run signal "+run.signal);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
