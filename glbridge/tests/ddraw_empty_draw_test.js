"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),crypto=require('node:crypto');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_EMPTY_SOURCE||root+'/ddrawproxy/d3d7_proxy.inc','utf8'),fixturePath=path.resolve(root,'../../v86/tests/glbridge/fixtures/ddraw_native_empty.json'),native=JSON.parse(fs.readFileSync(fixturePath)),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-empty-'));
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');assert.equal(sha(root+'/../'+native.source),native.sourceSha256);
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return source.slice(m.index,end);}
const names=['up','strided','indexed','istrided','vb','ivb','indexed_v0','indexed_i0','istrided_v0','istrided_i0','ivb_v0','ivb_i0','null_vb','null_ivb'];
const rows=native.calls.filter(r=>r.name!=='clear');assert.equal(rows.length,252);
const code=String.raw`
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ROOT/d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD,UINT,D3DPRIMITIVETYPE;typedef int32_t HRESULT,BOOL;typedef uint16_t WORD;typedef unsigned char BYTE;
typedef struct {DWORD dwNumVertices,dwFVF;}D3DVERTEXBUFFERDESC;
typedef struct D3D7VertexBuffer{const int*lpVtbl;void*owner;BOOL locked;D3DVERTEXBUFFERDESC desc;BYTE*shadow;UINT stride,handle;}D3D7VertexBuffer,IDirect3DVertexBuffer7;
typedef struct{UINT value;}D9WGClipLegacyStatus;
typedef struct{void*owner,*legacy_material;UINT creation_version,wire_handle;BOOL render_target_invalid,render_target_pending;DWORD render_states[256];D9WGClipLegacyStatus clip_status;}D3D7Device,IDirect3DDevice7;
typedef struct{struct{void*lpvData;DWORD dwStride;}position;}D3DDRAWPRIMITIVESTRIDEDDATA;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define D3DERR_INVALIDVERTEXFORMAT ((HRESULT)0x88760800u)
#define D3DERR_INVALIDVERTEXTYPE ((HRESULT)0x887602e1u)
#define DDERR_INVALIDPARAMS ((HRESULT)0x80070057u)
#define DDERR_INVALIDPIXELFORMAT ((HRESULT)0x88760091u)
#define D3DERR_INITFAILED ((HRESULT)0x887602c2u)
#define D3DRENDERSTATE_LIGHTING 137u
#define D3DFVF_NORMAL 16u
#define D3DDP_DONOTLIGHT 4u
#define D3DPT_POINTLIST 1u
#define D3DPT_LINELIST 2u
#define D3DPT_LINESTRIP 3u
#define D3DPT_TRIANGLELIST 4u
#define D3DPT_TRIANGLESTRIP 5u
#define D3DPT_TRIANGLEFAN 6u
#define DDWG_TRACE_ERROR(h) (h)
#define FAILED(h) ((HRESULT)(h)<0)
#define SUCCEEDED(h) ((HRESULT)(h)>=0)
#define ZeroMemory(p,n) memset(p,0,n)
static const int g_d3d7_vb_vtbl=7;
static unsigned checks,draws,packs,commands,classified,dirty,states,bindings,allocations;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"empty draw line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static D3D7Device*d3d7_device_from_iface(IDirect3DDevice7*p){return p;}
static D3D7VertexBuffer*d3d7_vb_from_iface(IDirect3DVertexBuffer7*p){return p;}
static BOOL d3d7_emit_render_target(D3D7Device*d){bindings++;d->render_target_pending=FALSE;return TRUE;}
static HRESULT d3d7_device_SetRenderState(IDirect3DDevice7*d,DWORD s,DWORD v){(void)d;(void)s;(void)v;states++;return 0;}
static BOOL d3d7_emit_draw_up(D3D7Device*d,D3DPRIMITIVETYPE t,DWORD f,const void*v,DWORD n){(void)d;(void)t;(void)f;draws++;return !!v&&n>0;}
static BOOL d3d7_emit_draw_indexed_up(D3D7Device*d,D3DPRIMITIVETYPE t,DWORD f,const void*v,DWORD n,const WORD*i,DWORD c){(void)d;(void)t;(void)f;draws++;return !!v&&n>0&&!!i&&c>0;}
static BYTE*d3d7_pack_strided(DWORD f,D3DDRAWPRIMITIVESTRIDEDDATA*d,DWORD n,UINT*b){(void)f;packs++;if(!d||!d->position.lpvData||!n)return NULL;*b=n*12;BYTE*p=calloc(1,*b);if(p)allocations++;return p;}
static void heap_free(void*p){MUST(p&&allocations);allocations--;free(p);}
static BOOL d3d7_draw_clip_status(D3D7Device*d,DWORD f,const void*v,UINT n,const WORD*i,UINT c,BOOL screen,D9WGClipLegacyStatus*s){(void)d;(void)f;(void)v;(void)n;(void)i;(void)c;(void)screen;classified++;s->value=123;return TRUE;}
static HRESULT d3d7_clip_vb_draw(D3D7Device*d,D3D7VertexBuffer*b,D3DPRIMITIVETYPE t,UINT start,UINT n,const WORD*i,UINT c,BOOL*handled){(void)d;(void)b;(void)t;(void)start;(void)n;(void)i;(void)c;*handled=FALSE;return 0;}
static BOOL d3d7_fvf_uses_wrap(D3D7Device*d,DWORD f){(void)d;(void)f;return FALSE;}
static BOOL d3d7_emit_fvf(D3D7Device*d,DWORD f,UINT*s){(void)d;(void)f;if(s)*s=12;return TRUE;}
static BOOL emit_command(UINT op,const void*p,UINT n){(void)op;MUST(p&&n);commands++;return TRUE;}
static void d3d7_mark_draw_surfaces_dirty(D3D7Device*d){(void)d;dirty++;}
METHODS
static HRESULT call(D3D7Device*d,D3D7VertexBuffer*b,UINT kind,UINT index){float vertices[4][3]={{0}};WORD indices[4]={0,1,2,3};D3DDRAWPRIMITIVESTRIDEDDATA strided={0};const DWORD fvfs[]={2,0,0x80000002u,1,0x14u,0x1002u,0x8002u};UINT fvf=fvfs[(index%100)/10],type=index%10;
 switch(kind){case 0:return d3d7_device_DrawPrimitive(d,type,fvf,NULL,0,0);case 1:return d3d7_device_DrawPrimitiveStrided(d,type,fvf,&strided,0,0);case 2:return d3d7_device_DrawIndexedPrimitive(d,type,fvf,NULL,0,NULL,0,0);case 3:return d3d7_device_DrawIndexedPrimitiveStrided(d,type,fvf,&strided,0,NULL,0,0);case 4:return d3d7_device_DrawPrimitiveVB(d,type,b,0,0,0);case 5:return d3d7_device_DrawIndexedPrimitiveVB(d,type,b,0,0,NULL,0,0);case 6:return d3d7_device_DrawIndexedPrimitive(d,type,2,NULL,0,indices,4,0);case 7:return d3d7_device_DrawIndexedPrimitive(d,type,2,vertices,4,NULL,0,0);case 8:return d3d7_device_DrawIndexedPrimitiveStrided(d,type,2,&strided,0,indices,4,0);case 9:strided.position.lpvData=vertices;strided.position.dwStride=12;return d3d7_device_DrawIndexedPrimitiveStrided(d,type,2,&strided,4,NULL,0,0);case 10:return d3d7_device_DrawIndexedPrimitiveVB(d,type,b,0,0,indices,4,0);case 11:return d3d7_device_DrawIndexedPrimitiveVB(d,type,b,0,4,NULL,0,0);case 12:return d3d7_device_DrawPrimitiveVB(d,5,NULL,0,0,0);default:return d3d7_device_DrawIndexedPrimitiveVB(d,5,NULL,0,0,NULL,0,0);}
}
static const struct{UINT kind,index,expected;}observations[]={OBSERVATIONS};
int main(void){int owner;BYTE bytes[48]={0};D3D7Device d={.owner=&owner,.creation_version=7,.wire_handle=31,.clip_status={67}};D3D7VertexBuffer buffer={.lpVtbl=&g_d3d7_vb_vtbl,.owner=&owner,.desc={4,2},.shadow=bytes,.stride=12,.handle=41};
 for(UINT round=0;round<16;round++)for(UINT i=0;i<sizeof(observations)/sizeof(*observations);i++){const UINT expected=observations[i].expected;HRESULT hr=call(&d,&buffer,observations[i].kind,observations[i].index);if((UINT)hr!=expected)fprintf(stderr,"native case kind=%u index=%u expected=%08x got=%08x\n",observations[i].kind,observations[i].index,expected,(UINT)hr);MUST((UINT)hr==expected);MUST(!draws&&!packs&&!commands&&!classified&&!dirty&&!states&&!bindings&&!allocations&&d.clip_status.value==67);}
 float vertices[4][3]={{0}};WORD indices[3]={0,1,2};D3DDRAWPRIMITIVESTRIDEDDATA data={.position={vertices,12}};MUST(!d3d7_device_DrawPrimitive(&d,4,2,vertices,4,0));MUST(!d3d7_device_DrawIndexedPrimitive(&d,4,2,vertices,4,indices,3,0));MUST(!d3d7_device_DrawPrimitiveStrided(&d,4,2,&data,4,0));MUST(!d3d7_device_DrawIndexedPrimitiveStrided(&d,4,2,&data,4,indices,3,0));MUST(!d3d7_device_DrawPrimitiveVB(&d,4,&buffer,0,4,0));MUST(!d3d7_device_DrawIndexedPrimitiveVB(&d,4,&buffer,0,4,indices,3,0));MUST(draws==5&&packs==2&&commands==2&&classified==2&&dirty==1&&!allocations);MUST(d3d7_device_DrawPrimitive(&d,4,2,NULL,3,0)==D3DERR_INVALIDVERTEXFORMAT);MUST(d3d7_device_DrawIndexedPrimitive(&d,4,2,vertices,4,NULL,3,0)==D3DERR_INVALIDVERTEXFORMAT);
 printf("PASS actual public empty draw C: %u checks,252 original XP calls repeated16 times, six empty routes preserve all observable work and clip state, nonempty paths still dispatch\n",checks);return 0;}
`.replaceAll('ROOT',root).replace('METHODS',['d3d7_primitive_counts','d3d7_empty_draw_result','d3d7_prepare_legacy_draw','d3d7_device_DrawPrimitive','d3d7_device_DrawIndexedPrimitive','d3d7_device_DrawPrimitiveStrided','d3d7_device_DrawIndexedPrimitiveStrided','d3d7_device_DrawPrimitiveVB','d3d7_device_DrawIndexedPrimitiveVB'].map(extract).join('\n')).replace('OBSERVATIONS',rows.map(r=>'{'+[names.indexOf(r.name),r.index,r.hr+'u'].join(',')+'}').join(','));
try{const file=dir+'/empty.c',exe=dir+'/empty';fs.writeFileSync(file,code);const built=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',file,'-o',exe],{encoding:'utf8'});assert.equal(built.status,0,built.stderr);const r=spawnSync(exe,[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
