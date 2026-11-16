'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_LEGACY_BINDING_SOURCE||root+'/ddrawproxy/d3d_legacy_proxy.inc','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-legacy-binding-'));
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return source.slice(m.index,end).replace(/\bWINAPI\b/g,'');}
const prefix=String.raw`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,ULONG,D3DMATERIALHANDLE;typedef int32_t HRESULT;typedef int BOOL,WINBOOL;
typedef struct{unsigned id;}DDrawObject;
typedef struct D3DLegacyViewport D3DLegacyViewport;typedef struct D3DLegacyLight D3DLegacyLight;
typedef struct{const int*lpVtbl;}IDirect3DLight;
typedef D3DLegacyViewport IDirect3DViewport3;
typedef struct{D3DLegacyViewport*legacy_current_viewport;}D3D7Device;
struct D3DLegacyViewport{DDrawObject*owner;D3D7Device*device;D3DLegacyLight*lights;DWORD background;BOOL background_valid;};
struct D3DLegacyLight{const int*vtbl;DDrawObject*owner;D3DLegacyViewport*viewport;D3DLegacyLight*next_in_viewport;ULONG ref;};
static const int g_d3d_light_vtbl=1,bad_vtbl=2;static unsigned checks,applies,adds,releases;static HRESULT fail_apply;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"legacy binding line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static D3DLegacyViewport*d3d_viewport_from_any(IDirect3DViewport3*p){return p;}
static D3DLegacyLight*d3d_light_from_iface(IDirect3DLight*p){return(void*)p;}
static void*d3d_legacy_find_material(DDrawObject*o,DWORD handle){return handle==0x100u+o->id?o:NULL;}
static ULONG d3d_light_AddRef(IDirect3DLight*p){D3DLegacyLight*l=(void*)p;MUST(l->ref&&l->viewport);adds++;return++l->ref;}
static ULONG d3d_light_Release(IDirect3DLight*p){D3DLegacyLight*l=(void*)p;MUST(l->ref&&!l->viewport&&!l->next_in_viewport);releases++;return--l->ref;}
static HRESULT d3d_legacy_apply_viewport(D3DLegacyViewport*v){MUST(v->device&&v->device->legacy_current_viewport==v);applies++;return fail_apply;}
`;
const tests=String.raw`
static void background_cases(void){DDrawObject owner={7};D3DLegacyViewport v={.owner=&owner};for(unsigned round=0;round<128;round++){
 DWORD out=0xdead;BOOL valid=9;MUST(!d3d_viewport_GetBackground(&v,&out,&valid)&&out==v.background&&valid==v.background_valid);D3DLegacyViewport before=v;
 const DWORD rejected[]={0,1,0xffffffff,0x106};for(unsigned i=0;i<4;i++)MUST(d3d_viewport_SetBackground(&v,rejected[i])==DDERR_INVALIDPARAMS&&!memcmp(&v,&before,sizeof(v)));
 MUST(!d3d_viewport_SetBackground(&v,0x107)&&v.background==0x107&&v.background_valid);before=v;MUST(d3d_viewport_SetBackground(&v,0)==DDERR_INVALIDPARAMS&&!memcmp(&v,&before,sizeof(v)));MUST(!d3d_viewport_GetBackground(&v,&out,&valid)&&out==0x107&&valid);out=0xdead;valid=9;MUST(d3d_viewport_GetBackground(&v,NULL,&valid)==DDERR_INVALIDPARAMS&&valid==9);MUST(d3d_viewport_GetBackground(&v,&out,NULL)==DDERR_INVALIDPARAMS&&out==0xdead);
 }}
static void light_cases(void){for(unsigned round=0;round<128;round++)for(unsigned active=0;active<2;active++){
 DDrawObject owner={7},other={8};D3DLegacyViewport v={.owner=&owner},w={.owner=&owner};D3D7Device device={active?&v:&w};v.device=w.device=&device;D3DLegacyLight a={.vtbl=&g_d3d_light_vtbl,.owner=&owner,.ref=1},b=a,c=a;unsigned old=applies;
 MUST(!d3d_viewport_AddLight(&v,(void*)&a)&&v.lights==&a&&a.viewport==&v&&a.ref==2&&applies==old+active);D3DLegacyLight before=a;D3DLegacyViewport prior=v;unsigned ad=adds,re=releases;old=applies;
 MUST(d3d_viewport_AddLight(&v,(void*)&a)==D3DERR_LIGHTHASVIEWPORT&&!memcmp(&a,&before,sizeof(a))&&!memcmp(&v,&prior,sizeof(v))&&adds==ad&&releases==re&&applies==old);MUST(d3d_viewport_AddLight(&w,(void*)&a)==D3DERR_LIGHTHASVIEWPORT&&!w.lights&&a.ref==2);
 MUST(!d3d_viewport_AddLight(&v,(void*)&b)&&v.lights==&b&&b.next_in_viewport==&a&&b.ref==2);old=applies;fail_apply=DDERR_GENERIC;MUST(d3d_viewport_AddLight(&v,(void*)&c)==(active?DDERR_GENERIC:0)&&v.lights==&c&&c.ref==2&&applies==old+active);fail_apply=0;
 MUST(d3d_viewport_DeleteLight(&w,(void*)&a)==D3DERR_LIGHTNOTINTHISVIEWPORT&&a.ref==2);MUST(!d3d_viewport_DeleteLight(&v,(void*)&b)&&v.lights==&c&&c.next_in_viewport==&a&&b.ref==1);MUST(!d3d_viewport_DeleteLight(&v,(void*)&c)&&v.lights==&a&&c.ref==1);MUST(!d3d_viewport_DeleteLight(&v,(void*)&a)&&!v.lights&&a.ref==1);MUST(d3d_viewport_DeleteLight(&v,(void*)&a)==D3DERR_LIGHTNOTINTHISVIEWPORT);
 a.owner=&other;MUST(d3d_viewport_AddLight(&v,(void*)&a)==DDERR_INVALIDPARAMS&&!v.lights&&a.ref==1);a.owner=&owner;a.vtbl=&bad_vtbl;MUST(d3d_viewport_AddLight(&v,(void*)&a)==DDERR_INVALIDPARAMS);MUST(d3d_viewport_DeleteLight(&v,(void*)&a)==DDERR_INVALIDPARAMS);MUST(d3d_viewport_AddLight(&v,NULL)==DDERR_INVALIDPARAMS&&d3d_viewport_DeleteLight(&v,NULL)==DDERR_INVALIDPARAMS);
 }}
int main(void){background_cases();light_cases();printf("PASS actual SDK legacy bindings: %u checks, zero/invalid background preservation, duplicate/cross-viewport light rejection, references/list order/current viewport, failed update prefix and detach cleanup\n",checks);return 0;}
`;
try{fs.writeFileSync(dir+'/sdk.c','#include <d3d.h>\n#define DDWG_TRACE_ERROR(h) (h)\nstatic int binding_begin;\n'+['d3d_viewport_SetBackground','d3d_viewport_GetBackground','d3d_viewport_AddLight','d3d_viewport_DeleteLight'].map(extract).join('\n')+tests+'\nstatic int binding_end;');const pp=spawnSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-P',dir+'/sdk.c'],{encoding:'utf8',maxBuffer:16<<20});assert.equal(pp.status,0,pp.stderr);const body=pp.stdout.split('static int binding_begin;')[1].split('static int binding_end;')[0];fs.writeFileSync(dir+'/test.c',prefix+body);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
