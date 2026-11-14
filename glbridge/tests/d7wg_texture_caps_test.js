'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.D7WG_TEXTURE_CAPS_SOURCE||root+'/ddrawproxy/d3d7_proxy.inc','utf8'),surface=fs.readFileSync(root+'/ddrawproxy/ddraw_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d7wg-texture-caps-'));
function extract(text,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(text);assert(m,name);let at=text.indexOf('{',m.index)+1,d=1;for(;d;at++){if(text[at]==='{')d++;if(text[at]==='}')d--;}return text.slice(m.index,at);}
const code=`#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef uint32_t UINT,DWORD,ULONG;typedef int32_t HRESULT;typedef int BOOL;typedef size_t SIZE_T;typedef unsigned char BYTE;typedef struct{UINT id;}GUID;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define DD_VERSION_1 1u
#define DD_VERSION_2 2u
#define DD_VERSION_3 3u
#define DD_VERSION_4 4u
#define DD_VERSION_7 7u
#define DDERR_INVALIDPARAMS (-1)
#define D3DERR_INITFAILED (-2)
#define D3D_OK 0
#define D3D7_MAX_TEXTURE_STAGES 8u
#define DDSCAPS_TEXTURE 0x1000u
#define DDWG_TRACE_ERROR(h) (h)
#define ZeroMemory(p,n) memset(p,0,n)
typedef struct{const void*lpVtbl;}IDirectDrawSurface7;
typedef struct{UINT id;}DDrawObject;
typedef struct DDSurface{const void*vtbl7,*vtbl4,*vtbl3,*vtbl2,*vtbl1;DDrawObject*owner;UINT refs,format,handle;struct{struct{DWORD dwCaps;}ddsCaps;}desc;}DDSurface;
typedef struct{DDSurface*textures[8];BYTE texture_mask[8];}D3D7StateBlock;
typedef struct{DDrawObject*owner;UINT creation_version,wire_handle;GUID device_guid;DDSurface*textures[8];D3D7StateBlock*recording_state_block;}D3D7Device,IDirect3DDevice7;
static const int g_surface_vtbl=7,g_surface_vtbl4=4,g_surface_vtbl3=3,g_surface_vtbl2=2,g_surface_vtbl1=1;static const GUID IID_IDirect3DRGBDevice={101};
static UINT checks,emits,reads,prepares,rebinds,fixups;static BOOL fail_emit;static DWORD wire_texture;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"texture caps line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static BOOL guid_equal(const GUID*a,const GUID*b){return a->id==b->id;}
${extract(surface,'surface_from_any')}
static D3D7Device*d3d7_device_from_iface(IDirect3DDevice7*p){return p;}
static BOOL format_is_depth(UINT f){return f==75;}
static ULONG surface_AddRef(IDirectDrawSurface7*p){DDSurface*s=surface_from_any(p,NULL);MUST(s->refs);return ++s->refs;}
static ULONG surface_Release(IDirectDrawSurface7*p){DDSurface*s=surface_from_any(p,NULL);MUST(s->refs>1);return --s->refs;}
static BOOL sync_external_surface_for_read(DDSurface*s){MUST(s);reads++;return TRUE;}
static BOOL prepare_surface_for_host(DDSurface*s){MUST(s);prepares++;return TRUE;}
static void rebind_host_surface_state(DDSurface*s){MUST(s);rebinds++;}
static BOOL d3d7_fixup_legacy_texture_alpha(D3D7Device*d){MUST(d);fixups++;return TRUE;}
static BOOL emit_command(UINT op,const void*p,UINT n){const D9WGSetTexture*q=p;MUST(op==D9WG_OP_SET_TEXTURE&&n==sizeof(*q)&&q->device_handle==33&&!q->stage);emits++;if(fail_emit)return FALSE;wire_texture=q->texture_handle;return TRUE;}
${extract(source,'d3d7_sampled_texture')}
${extract(source,'d3d7_device_SetTexture')}
${extract(source,'d3d7_device_GetTexture')}
int main(void){(void)&IID_IDirect3DRGBDevice;(void)&guid_equal;const UINT versions[]={1,2,3,7};
 for(UINT v=0;v<4;v++)for(UINT rgb=0;rgb<2;rgb++)for(UINT caps=0;caps<2;caps++)for(UINT view=0;view<5;view++)for(UINT recording=0;recording<2;recording++){
  DDrawObject owner={1};D3D7Device device={.owner=&owner,.wire_handle=33,.creation_version=versions[v],.device_guid={rgb?101:102}};DDSurface s={.vtbl7=&g_surface_vtbl,.vtbl4=&g_surface_vtbl4,.vtbl3=&g_surface_vtbl3,.vtbl2=&g_surface_vtbl2,.vtbl1=&g_surface_vtbl1,.owner=&owner,.refs=1,.format=21,.handle=44,.desc={.ddsCaps={.dwCaps=caps?DDSCAPS_TEXTURE:0}}};D3D7StateBlock block={0};if(recording)device.recording_state_block=&block;
  void*views[]={&s.vtbl7,&s.vtbl4,&s.vtbl3,&s.vtbl2,&s.vtbl1};IDirectDrawSurface7*iface=views[view];BOOL keeps=caps||versions[v]<7,binds=caps||(rgb&&versions[v]<7);UINT oldreads=reads,oldprep=prepares,oldrebind=rebinds,oldemit=emits;
  MUST(!d3d7_device_SetTexture(&device,0,iface));MUST(s.refs==1u+keeps);
  if(recording){MUST(!device.textures[0]&&block.textures[0]==(keeps?&s:NULL)&&block.texture_mask[0]&&reads==oldreads&&prepares==oldprep&&rebinds==oldrebind&&emits==oldemit);if(block.textures[0])surface_Release((void*)&block.textures[0]->vtbl7);}
  else{MUST(device.textures[0]==(keeps?&s:NULL)&&reads==oldreads+binds&&prepares==oldprep+binds&&rebinds==oldrebind+binds&&emits==oldemit+keeps);IDirectDrawSurface7*out=(void*)1;MUST(!d3d7_device_GetTexture(&device,0,&out)&&out==(keeps?(void*)&s.vtbl7:NULL));if(out)surface_Release(out);MUST(wire_texture==(binds?44u:0u));MUST(!d3d7_device_SetTexture(&device,0,NULL)&&s.refs==1&&!device.textures[0]);}
 }
 DDrawObject owner={1},other={2};D3D7Device device={.owner=&owner,.wire_handle=33,.creation_version=7};DDSurface s={.vtbl7=&g_surface_vtbl,.owner=&owner,.refs=1,.format=21,.handle=44,.desc={.ddsCaps={.dwCaps=DDSCAPS_TEXTURE}}};
 UINT before=emits;fail_emit=TRUE;MUST(d3d7_device_SetTexture(&device,0,(void*)&s)==D3DERR_INITFAILED&&s.refs==1&&!device.textures[0]);fail_emit=FALSE;MUST(emits==before+1);
 s.owner=&other;before=emits;MUST(d3d7_device_SetTexture(&device,0,(void*)&s)==DDERR_INVALIDPARAMS&&emits==before&&s.refs==1);s.desc.ddsCaps.dwCaps=0;MUST(d3d7_device_SetTexture(&device,0,(void*)&s)==DDERR_INVALIDPARAMS&&emits==before&&s.refs==1);s.owner=&owner;s.format=75;MUST(d3d7_device_SetTexture(&device,0,(void*)&s)==DDERR_INVALIDPARAMS);s.format=21;s.vtbl7=NULL;MUST(d3d7_device_SetTexture(&device,0,(void*)&s)==DDERR_INVALIDPARAMS);s.vtbl7=(void*)(uintptr_t)0xbad;MUST(d3d7_device_SetTexture(&device,0,(void*)&s)==DDERR_INVALIDPARAMS);MUST(d3d7_device_SetTexture(&device,8,NULL)==DDERR_INVALIDPARAMS);
 MUST(fixups&&prepares&&reads&&rebinds&&wire_texture==0);printf("PASS actual SetTexture caps C: %u checks, five real surface offsets, legacy HAL/RGB versus modern policy, recording, query references, failure rollback, owner/depth/stage/foreign-vtable validation\\n",checks);return 0;}
`;
try{fs.writeFileSync(dir+'/caps.c',code);const r=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/caps.c','-o',dir+'/caps'],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);const run=spawnSync(dir+'/caps',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
