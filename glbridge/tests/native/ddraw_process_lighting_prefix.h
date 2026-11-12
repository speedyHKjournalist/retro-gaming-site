#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_math.h"
#include "runtime/d9wg_fvf.h"
typedef uint32_t DWORD,UINT;typedef int32_t HRESULT,BOOL;typedef unsigned char BYTE;
typedef struct{float r,g,b,a;}Color;
typedef struct{Color diffuse,ambient,specular,emissive;float power;}D3DMATERIAL7;
typedef struct{float v[16];}D3DMATRIX;
typedef struct{DWORD dwX,dwY,dwWidth,dwHeight;float dvMinZ,dvMaxZ;}D3DVIEWPORT7;
typedef struct{float dvGuardBandLeft,dvGuardBandTop,dvGuardBandRight,dvGuardBandBottom;}D3DDEVICEDESC7;
typedef struct{DWORD dwSize,dwCaps,dwFVF,dwNumVertices;}D3DVERTEXBUFFERDESC;
typedef struct{float x,y,z;}Vector;
typedef struct{DWORD dltType;Color dcvDiffuse,dcvSpecular,dcvAmbient;Vector dvPosition,dvDirection;float dvRange,dvFalloff,dvAttenuation0,dvAttenuation1,dvAttenuation2,dvTheta,dvPhi;}D3DLIGHT7;
#define D9WG_LIGHTING_SDK_MATERIAL D3DMATERIAL7
#define D9WG_LIGHTING_SDK_LIGHT D3DLIGHT7
#define D9WG_LIGHTING_LEGACY7
#include "runtime/d9wg_lighting_bridge.h"
#define D9WG_LIGHT_ALLOC(n) calloc(1,n)
#define D9WG_LIGHT_FREE(p) free(p)
#include "runtime/d9wg_light_state.h"
typedef struct{BOOL material_mask;D3DMATERIAL7 material;}D3D7StateBlock;
typedef struct D3D7Device{UINT creation_version;DWORD wire_handle;D3DMATERIAL7 material;float process_specular_power;DWORD render_states[256],texture_stage_states[8][33];D3DMATRIX transforms[24];D3DVIEWPORT7 viewport;D3DLIGHT7 lights[8];BOOL light_set[8],light_enabled[8];D9WGLightState*extra_lights;D3D7StateBlock*recording_state_block;float clip_planes[6][4];D9WGClipLegacyStatus clip_status;int device_guid;}D3D7Device,IDirect3DDevice7;
typedef struct{D3DVERTEXBUFFERDESC desc;BYTE*shadow;UINT stride,byte_count,handle;BOOL locked;D9WGClipBuffer clips;}D3D7VertexBuffer;
static unsigned checks,emits,uploads,fail_emit,fail_upload;static D9WGSetMaterial wire_material;static UINT uploaded_offset,uploaded_bytes;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"process lighting line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static D3D7Device*d3d7_device_from_iface(IDirect3DDevice7*p){return p;}
static BOOL emit_command(UINT op,const void*p,UINT n){MUST(op==D9WG_OP_SET_MATERIAL&&n==sizeof(wire_material));emits++;if(fail_emit)return 0;memcpy(&wire_material,p,n);return 1;}
static BOOL emit_buffer_update(UINT handle,UINT offset,const void*p,UINT n,UINT flags){MUST(handle==1&&p&&flags==0);uploads++;uploaded_offset=offset;uploaded_bytes=n;return!fail_upload;}
/* No blending/clip allocations in this lighting integration fixture. Real
 * shared vertex, FVF, matrix, fog, colour and light kernels are linked. */
static uint32_t d9wg_vertex_windows_blend(D9WGVertexState*s,const void*r,const void*w,uint32_t c,uint32_t gap,const void*v,const void*p){(void)s;(void)r;(void)w;(void)c;(void)gap;(void)v;(void)p;return 0;}
static void d9wg_vertex_windows_blend_free(D9WGVertexState*s){MUST(!s->blend);}
static uint32_t d9wg_vertex_windows_clip(D9WGVertexState*s,BOOL clip,uint32_t pos,uint32_t stride,uint32_t count,const D9WGProcessViewport*v,const float*g,D9WGClipBatch**b){(void)s;(void)clip;(void)pos;(void)stride;(void)count;(void)v;(void)g;*b=NULL;return 0;}
static void d9wg_vertex_windows_clip_release(void*c,void*p){(void)c;free(p);}
static void d3d7_fill_caps_for(const void*g,D3DDEVICEDESC7*c){(void)g;c->dvGuardBandLeft=c->dvGuardBandTop=-32768;c->dvGuardBandRight=c->dvGuardBandBottom=32768;}
