"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), current = fs.readFileSync(root + "/ddrawproxy/d3d_legacy_proxy.inc", "utf8"), source = fs.readFileSync(process.env.D7WG_LEGACY_VIEWPORT_SOURCE || root + "/ddrawproxy/d3d_legacy_proxy.inc", "utf8"), seven = fs.readFileSync(root + "/ddrawproxy/d3d7_proxy.inc", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d7wg-legacy-viewport-"));
function extract(name, input = source) {const re = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{"); let m = re.exec(input); if (!m) {input = current; m = re.exec(input);} assert(m, name); let end = input.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (input[end] === "{") depth++; if (input[end] === "}") depth--;} return input.slice(m.index, end);}
const code = `#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "${root}/runtime/d9wg_math.h"
#include "${root}/runtime/d9wg_legacy_viewport.h"
typedef uint32_t DWORD;typedef uint16_t WORD;typedef uint8_t BYTE;typedef int32_t LONG;typedef int BOOL,HRESULT,D3DTRANSFORMSTATETYPE;
typedef struct {float _11,_12,_13,_14,_21,_22,_23,_24,_31,_32,_33,_34,_41,_42,_43,_44;} D3DMATRIX;
typedef struct {DWORD dwX,dwY,dwWidth,dwHeight;float dvMinZ,dvMaxZ;} D3DVIEWPORT7;
typedef struct {DWORD dwSize,dwX,dwY,dwWidth,dwHeight;float dvScaleX,dvScaleY,dvMaxX,dvMaxY,dvMinZ,dvMaxZ;} D3DVIEWPORT;
typedef struct {DWORD dwSize,dwX,dwY,dwWidth,dwHeight;float dvClipX,dvClipY,dvClipWidth,dvClipHeight,dvMinZ,dvMaxZ;} D3DVIEWPORT2;
typedef struct {float x,y,z;} D3DVECTOR;
typedef struct {float x,y,z,nx,ny,nz,tu,tv;} D3DVERTEX;
typedef struct {float x,y,z;DWORD reserved,color,specular;float tu,tv;} D3DLVERTEX;
typedef struct {float sx,sy,sz,rhw;DWORD color,specular;float tu,tv;} D3DTLVERTEX;
typedef struct {DWORD dwFlags;float hx,hy,hz;} D3DHVERTEX;
typedef struct {DWORD dwSize;void*lpIn;DWORD dwInSize;void*lpOut;DWORD dwOutSize;D3DHVERTEX*lpHOut;DWORD dwClipUnion,dwClipIntersection;} D3DTRANSFORMDATA;
typedef struct {LONG x1,y1,x2,y2;} D3DRECT;
typedef struct {DWORD dwFlags;WORD wStart,wDest;DWORD dwCount,dwReserved;} D3DPROCESSVERTICES;
typedef struct {D3DMATRIX transforms[24],legacy_projection,legacy_clipspace;D3DVIEWPORT7 viewport;void*recording_state_block;} D3D7Device;
typedef D3D7Device IDirect3DDevice7,IDirect3DDevice3,IDirect3DDevice2;
typedef struct {D3D7Device*device;D3DVIEWPORT viewport1;D3DVIEWPORT2 viewport2;BOOL viewport1_set,viewport2_set;} D3DLegacyViewport;
typedef D3DLegacyViewport IDirect3DViewport3;
typedef struct {D3D7Device*device;struct {DWORD dwVertexCount,dwVertexOffset;struct {D3DRECT drExtent;} dsStatus;} data;DWORD byte_count,processed_count;BYTE*memory;D3DTLVERTEX*processed;} D3DLegacyExecuteBuffer;
typedef struct {unsigned prepared;} D9WGTNLLighting;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define D3DERR_EXECUTE_FAILED (-2)
#define D3DERR_VIEWPORTHASNODEVICE (-3)
#define D3DERR_VIEWPORTDATANOTSET (-4)
#define D3DTRANSFORMSTATE_WORLD 1
#define D3DTRANSFORMSTATE_VIEW 2
#define D3DTRANSFORMSTATE_PROJECTION 3
#define D3DTRANSFORM_CLIPPED 1u
#define D3DTRANSFORM_UNCLIPPED 2u
#define D3DCLIP_LEFT 1u
#define D3DCLIP_RIGHT 2u
#define D3DCLIP_TOP 4u
#define D3DCLIP_BOTTOM 8u
#define D3DCLIP_FRONT 16u
#define D3DCLIP_BACK 32u
#define D3DPROCESSVERTICES_TRANSFORMLIGHT 0u
#define D3DPROCESSVERTICES_TRANSFORM 1u
#define D3DPROCESSVERTICES_COPY 2u
#define D3DPROCESSVERTICES_OPMASK 7u
#define D3DPROCESSVERTICES_UPDATEEXTENTS 8u
#define D3DPROCESSVERTICES_NOCOLOR 16u
#define DDWG_TRACE_ERROR(x) (x)
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(d,s,n) memcpy(d,s,n)
static unsigned checks,prepares,lights,viewport_calls,transform_calls,fail_viewport,fail_transform,fail_light;
${["d3d7_identity_matrix", "d3d7_multiply_matrix", "d3d7_transform_vector"].map(n => extract(n, seven)).join("\n")}
static HRESULT d3d7_device_SetTransform(IDirect3DDevice7*d,D3DTRANSFORMSTATETYPE s,D3DMATRIX*m){transform_calls++;if(fail_transform){fail_transform--;return DDERR_INVALIDPARAMS;}if(!d->recording_state_block)d->transforms[s]=*m;return 0;}
static HRESULT d3d7_device_GetTransform(IDirect3DDevice7*d,D3DTRANSFORMSTATETYPE s,D3DMATRIX*m){*m=d->transforms[s];return 0;}
static HRESULT d3d7_device_MultiplyTransform(IDirect3DDevice7*d,D3DTRANSFORMSTATETYPE s,D3DMATRIX*m){D3DMATRIX out;d3d7_multiply_matrix(&d->transforms[s],m,&out);return d3d7_device_SetTransform(d,s,&out);}
static HRESULT d3d7_device_SetViewport(IDirect3DDevice7*d,D3DVIEWPORT7*v){viewport_calls++;if(fail_viewport){fail_viewport--;return DDERR_INVALIDPARAMS;}if(v->dvMinZ<0||v->dvMaxZ>1)return DDERR_INVALIDPARAMS;if(!d->recording_state_block)d->viewport=*v;return 0;}
static D3DLegacyViewport*d3d_viewport_from_any(IDirect3DViewport3*p){return p;}
static D3D7Device*d3d3_device_from_iface(IDirect3DDevice3*p){return p;}
static D3D7Device*d3d2_device_from_iface(IDirect3DDevice2*p){return p;}
static BOOL d3d7_prepare_lighting(D3D7Device*d,const D3DMATRIX*m,BOOL enabled,D9WGTNLLighting*s){(void)d;(void)m;(void)enabled;prepares++;s->prepared=1;return !fail_light;}
static void d9wg_lighting_vertex(const D9WGTNLLighting*s,const D3DMATRIX*m,const float*p,const float*n,const void*a,const void*b,uint32_t*c,uint32_t*spec){(void)m;(void)p;(void)n;(void)a;(void)b;lights+=s->prepared;*c=0xff123456;*spec=0x00112233;}
${["d3d_legacy_viewport7", "d3d_legacy_viewport_clipspace", "d3d_legacy_apply_viewport_projection", "d3d_legacy_set_transform", "d3d_legacy_get_transform", "d3d_legacy_multiply_transform", "d3d3_device_SetTransform", "d3d3_device_GetTransform", "d3d3_device_MultiplyTransform", "d3d2_device_SetTransform", "d3d2_device_GetTransform", "d3d2_device_MultiplyTransform", "d3d_viewport_TransformVertices", "d3d_execute_process_vertices"].map(n => extract(n)).join("\n")}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"legacy viewport line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
#define NEAR(a,b) CHECK(fabsf((a)-(b))<.0005f)
static void seed(D3D7Device*d,D3DLegacyViewport*v){memset(d,0,sizeof(*d));memset(v,0,sizeof(*v));for(unsigned i=0;i<24;i++)d3d7_identity_matrix(&d->transforms[i]);d3d7_identity_matrix(&d->legacy_projection);d3d7_identity_matrix(&d->legacy_clipspace);v->device=d;v->viewport1_set=1;v->viewport1=(D3DVIEWPORT){sizeof(D3DVIEWPORT),20,20,200,400,20,50,999,999,-25,60};}
static int matrices(void){D3D7Device d;D3DLegacyViewport v;D3DMATRIX p,m,out;seed(&d,&v);d3d7_identity_matrix(&p);p._41=2;p._42=1;
 CHECK(!d3d3_device_SetTransform(&d,3,&p));CHECK(!d3d_legacy_apply_viewport_projection(&v));NEAR(d.transforms[3]._11,.2f);NEAR(d.transforms[3]._22,.25f);NEAR(d.transforms[3]._41,.4f);NEAR(d.transforms[3]._42,.25f);CHECK(d.viewport.dvMinZ==0&&d.viewport.dvMaxZ==1);CHECK(!d3d2_device_GetTransform(&d,3,&out)&&!memcmp(&p,&out,sizeof(p)));
 for(unsigned i=0;i<128;i++){CHECK(!d3d_legacy_apply_viewport_projection(&v));NEAR(d.transforms[3]._41,.4f);NEAR(d.transforms[3]._42,.25f);}
 d3d7_identity_matrix(&m);m._11=3;m._22=4;CHECK(!d3d2_device_MultiplyTransform(&d,3,&m));CHECK(!d3d3_device_GetTransform(&d,3,&out));NEAR(out._41,2);NEAR(out._42,1);NEAR(out._11,3);NEAR(d.transforms[3]._41,.4f);
 CHECK(!d3d3_device_MultiplyTransform(&d,3,&m));CHECK(!d3d2_device_SetTransform(&d,1,&p));CHECK(!d3d3_device_GetTransform(&d,1,&out)&&!memcmp(&out,&p,sizeof(p)));CHECK(!d3d2_device_MultiplyTransform(&d,1,&m));
 v.viewport2_set=1;v.viewport2=(D3DVIEWPORT2){sizeof(D3DVIEWPORT2),20,20,200,400,-.5f,4,5,10,0,2};CHECK(!d3d_legacy_apply_viewport_projection(&v));NEAR(d.transforms[3]._43,0);NEAR(d.transforms[3]._33,.5f);NEAR(d.transforms[3]._41,0);NEAR(d.transforms[3]._42,.4f);
 CHECK(!d3d3_device_SetTransform(&d,3,&p));NEAR(d.transforms[3]._41,0);NEAR(d.transforms[3]._42,.4f);out=d.legacy_projection;fail_transform=1;CHECK(d3d3_device_SetTransform(&d,3,&m)==DDERR_INVALIDPARAMS&&!memcmp(&out,&d.legacy_projection,sizeof(out)));
 out=d.legacy_clipspace;v.viewport2.dvClipX=3;fail_viewport=1;CHECK(d3d_legacy_apply_viewport_projection(&v)==DDERR_INVALIDPARAMS&&!memcmp(&out,&d.legacy_clipspace,sizeof(out)));fail_transform=1;CHECK(d3d_legacy_apply_viewport_projection(&v)==DDERR_INVALIDPARAMS&&!memcmp(&out,&d.legacy_clipspace,sizeof(out)));
 d.recording_state_block=&d;out=d.legacy_projection;CHECK(!d3d3_device_SetTransform(&d,3,&m)&&!memcmp(&out,&d.legacy_projection,sizeof(out)));out=d.legacy_clipspace;CHECK(!d3d_legacy_apply_viewport_projection(&v)&&!memcmp(&out,&d.legacy_clipspace,sizeof(out)));CHECK(d3d_legacy_get_transform(&d,3,NULL)==DDERR_INVALIDPARAMS&&d3d_legacy_set_transform(&d,3,NULL)==DDERR_INVALIDPARAMS&&d3d_legacy_multiply_transform(&d,3,NULL)==DDERR_INVALIDPARAMS);return 0;}
static int wine_vectors(void){D3D7Device d;D3DLegacyViewport v;seed(&d,&v);D3DLVERTEX in[6]={{0,0,0,0,1,2,3,4},{1,1,1,0,6,7,8,9},{-1,-1,-1,0,11,12,13,14},{.5f,.5f,.5f,0,16,17,18,19},{-.5f,-.5f,-.5f,0,21,22,23,24},{-.5f,-.5f,0,0,26,27,28,29}};struct{D3DTLVERTEX v;DWORD guard[2];}out[6];D3DHVERTEX h[6],before[6];DWORD off=0;
 D3DTRANSFORMDATA data={sizeof(data),in,sizeof(in[0]),out,sizeof(out[0]),h,0,0};d.transforms[1]._41=1;d.transforms[2]._11=d.transforms[2]._22=d.transforms[2]._33=2;d.legacy_projection._42=1;
 v.viewport2_set=1;v.viewport2=(D3DVIEWPORT2){sizeof(D3DVIEWPORT2),20,20,200,400,-.5f,4,5,10,0,2};const float want[6][4]={{120,140,0,1},{200,60,1,1},{40,220,-1,1},{160,100,.5f,1},{80,180,-.5f,1},{80,180,0,1}};
 memset(out,0xa5,sizeof(out));memset(h,0x5a,sizeof(h));memcpy(before,h,sizeof(h));D3DMATRIX old=d.transforms[3];CHECK(!d3d_viewport_TransformVertices(&v,6,&data,D3DTRANSFORM_UNCLIPPED,&off)&&off==0&&!memcmp(before,h,sizeof(h))&&!memcmp(&old,&d.transforms[3],sizeof(old)));
 for(unsigned i=0;i<6;i++){float actual[4];memcpy(actual,&out[i].v,16);for(unsigned j=0;j<4;j++)NEAR(actual[j],want[i][j]);CHECK(!memcmp((BYTE*)&out[i].v+16,(BYTE*)&in[i]+16,16));CHECK(out[i].guard[0]==0xa5a5a5a5&&out[i].guard[1]==0xa5a5a5a5);}
 d3d7_identity_matrix(&d.transforms[1]);d3d7_identity_matrix(&d.transforms[2]);d3d7_identity_matrix(&d.legacy_projection);d.legacy_projection._42=19.2f;d.legacy_projection._44=2;
 v.viewport2=(D3DVIEWPORT2){sizeof(D3DVIEWPORT2),0,0,1,1,-12.8f,22.4f,25.6f,25.6f,0,.5f};D3DLVERTEX clip[4]={{25.59f,25.59f,1,0,1,2,3,4},{25.61f,25.61f,1.01f,0,1,2,3,4},{-25.59f,-25.59f,0,0,1,2,3,4},{-25.61f,-25.61f,-.01f,0,1,2,3,4}};const float raw[4][3]={{25.59f,44.79f,1},{25.61f,44.81f,1.01f},{-25.59f,-6.39f,0},{-25.61f,-6.41f,-.01f}};const DWORD flags[]={0,2|4|32,0,1|8|16};data.lpIn=clip;memset(out,0xa5,sizeof(out));CHECK(!d3d_viewport_TransformVertices(&v,4,&data,D3DTRANSFORM_CLIPPED,&off)&&!off);for(unsigned i=0;i<4;i++){CHECK(h[i].dwFlags==flags[i]);NEAR(h[i].hx,raw[i][0]);NEAR(h[i].hy,raw[i][1]);NEAR(h[i].hz,raw[i][2]);if(flags[i])CHECK(out[i].v.color==0xa5a5a5a5&&out[i].v.specular==0xa5a5a5a5);}
 data.lpIn=&clip[3];CHECK(!d3d_viewport_TransformVertices(&v,1,&data,D3DTRANSFORM_CLIPPED,&off)&&off==(1|8|16));CHECK(!d3d_viewport_TransformVertices(&v,0,&data,D3DTRANSFORM_CLIPPED,&off)&&off==~0u);CHECK(d3d_viewport_TransformVertices(&v,1,&data,0,&off)==DDERR_INVALIDPARAMS);data.dwOutSize=31;CHECK(d3d_viewport_TransformVertices(&v,1,&data,2,&off)==DDERR_INVALIDPARAMS);return 0;}
static int execute(void){D3D7Device d;D3DLegacyViewport v;seed(&d,&v);CHECK(!d3d_legacy_apply_viewport_projection(&v));D3DLVERTEX in[4]={{0,0,.5f,0,0xff102030,0x00405060,.25f,.75f},{1,1,1,0,0xffa02010,0x00708090,.5f,1},{-1,-1,0,0,0xff206040,0x00908070,1,.5f},{0,0,0,0,0,0,0,0}};D3DTLVERTEX out[4];D3DLegacyExecuteBuffer b={.device=&d,.data={.dwVertexCount=4},.byte_count=sizeof(in),.processed_count=4,.memory=(BYTE*)in,.processed=out};D3DPROCESSVERTICES p={.dwFlags=1|8,.dwCount=3};
 for(unsigned round=0;round<128;round++){memset(out,0xa5,sizeof(out));unsigned n=prepares,l=lights;fail_light=1;CHECK(!d3d_execute_process_vertices(&b,&p)&&prepares==n&&lights==l);for(unsigned i=0;i<3;i++){CHECK(out[i].color==in[i].color&&out[i].specular==in[i].specular&&out[i].tu==in[i].tu&&out[i].tv==in[i].tv);NEAR(out[i].sx,120+in[i].x*20);NEAR(out[i].sy,220-in[i].y*50);NEAR(out[i].sz,in[i].z);}CHECK(b.data.dsStatus.drExtent.x1==100&&b.data.dsStatus.drExtent.x2==140&&b.data.dsStatus.drExtent.y1==170&&b.data.dsStatus.drExtent.y2==270);}
 p.dwFlags=1|16;memset(out,0xa5,sizeof(out));CHECK(!d3d_execute_process_vertices(&b,&p)&&out[0].color==0xa5a5a5a5&&out[0].specular==0xa5a5a5a5);p.dwFlags=0|16;CHECK(!d3d_execute_process_vertices(&b,&p)&&out[0].specular==0xa5a5a5a5);p.dwFlags=0;CHECK(d3d_execute_process_vertices(&b,&p)==DDERR_INVALIDPARAMS);fail_light=0;CHECK(!d3d_execute_process_vertices(&b,&p)&&out[0].color==0xff123456&&out[0].specular==0x00112233);
 p.dwFlags=2;CHECK(!d3d_execute_process_vertices(&b,&p)&&!memcmp(out,in,3*sizeof(*out)));p.dwFlags=1;p.wStart=3;CHECK(d3d_execute_process_vertices(&b,&p)==D3DERR_EXECUTE_FAILED);p.wStart=0;p.wDest=3;CHECK(d3d_execute_process_vertices(&b,&p)==D3DERR_EXECUTE_FAILED);p.wDest=0;b.byte_count=31;CHECK(d3d_execute_process_vertices(&b,&p)==D3DERR_EXECUTE_FAILED);b.byte_count=sizeof(in);b.data.dwVertexOffset=0xfffffff0u;CHECK(d3d_execute_process_vertices(&b,&p)==D3DERR_EXECUTE_FAILED);return 0;}
int main(void){(void)&d3d7_prepare_lighting;(void)&d3d_legacy_viewport_clipspace;(void)&d3d_legacy_get_transform;(void)&d3d_legacy_multiply_transform;CHECK(!matrices());CHECK(!wine_vectors());CHECK(!execute());printf("PASS actual legacy viewport and execute C: %u checks, 128 projection reactivations, 128 prelit passes, frozen Wine position/clip vectors, logical/effective matrices, payload guards, lighting isolation and bounded failure prefixes\\n",checks);return 0;}
`;
try {const file = dir + "/viewport.c", binary = dir + "/viewport"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", file, root + "/runtime/d9wg_math.c", "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
