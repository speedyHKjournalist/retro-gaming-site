'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync,execFileSync}=require('node:child_process');
const site=process.env.RETRO_GAMING_SITE||path.resolve(__dirname,'../..'),file=process.env.DDRAW_OVERLAY_CONTRACT_SOURCE||site+'/glbridge/ddrawproxy/ddraw_proxy.c',source=fs.readFileSync(file,'utf8');
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let e=source.indexOf('{',m.index)+1,d=1;for(;d;e++){if(source[e]==='{')d++;if(source[e]==='}')d--;}return source.slice(m.index,e);}
const sdk=execFileSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-dM','-x','c','-include','ddraw.h','-'],{input:'',encoding:'utf8',maxBuffer:16*1024*1024});
const flags=sdk.split('\n').filter(l=>/^#define (?:DDOVER_|DDOVERFX_|DDSCAPS_OVERLAY\s)/.test(l)).join('\n');
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-overlay-contract-'));
const code=String.raw`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "PROTOCOL"
typedef uint32_t DWORD;typedef int32_t LONG,HRESULT;typedef int BOOL;typedef struct{LONG left,top,right,bottom;}RECT,*LPRECT;typedef LONG*LPLONG;
typedef struct{DWORD dwColorSpaceLowValue,dwColorSpaceHighValue;}DDCOLORKEY;
typedef struct{DWORD dwSize,dwDDFX;DDCOLORKEY dckSrcColorkey,dckDestColorkey;}DDOVERLAYFX,*LPDDOVERLAYFX;
typedef struct DDrawObject{struct DDSurface*surfaces;}DDrawObject;
typedef struct DDSurface{const void*vtbl7;DDrawObject*owner;struct DDSurface*next,*overlay_destination;struct{struct{DWORD dwCaps;}ddsCaps;}desc;LONG overlay_z_order;DWORD color_key_present,overlay_wire_flags,width,height;RECT overlay_source_rect,overlay_destination_rect;BOOL overlay_visible,overlay_dirty;DDCOLORKEY overlay_source_key_override,overlay_destination_key_override;unsigned refs;}DDSurface,IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;
#define WINAPI
#define DD_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_NOTAOVERLAYSURFACE (-2)
#define DDERR_UNSUPPORTED (-3)
#define DDERR_GENERIC (-4)
#define DDERR_NOOVERLAYDEST (-5)
#define DDERR_INVALIDOBJECT (-6)
#define DDERR_INVALIDRECT (-7)
#define DDERR_NOCOLORKEY (-8)
#define DDERR_OVERLAYNOTVISIBLE (-9)
#define TRUE 1
#define FALSE 0
#define DDWG_TRACE_ERROR(hr) (hr)
#define UNSUPPORTED(msg,hr) (hr)
FLAGS
static unsigned checks,emits,addrefs,releases;static BOOL fail_emit;static DDSurface emitted;
#define MUST(x) do{++checks;if(!(x)){fprintf(stderr,"overlay contract line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static DDSurface*surface_from_iface(DDSurface*s){return s;}
static unsigned surface_AddRef(IDirectDrawSurface7*p){DDSurface*s=(void*)p;addrefs++;return ++s->refs;}
static unsigned surface_Release(IDirectDrawSurface7*p){DDSurface*s=(void*)p;releases++;MUST(s->refs);return --s->refs;}
static BOOL emit_overlay_state(DDSurface*s){emits++;emitted=*s;return !fail_emit;}
METHODS
static void cases(void){
 DDrawObject owner={0},foreign={0};DDSurface overlay={.owner=&owner,.desc={.ddsCaps={.dwCaps=DDSCAPS_OVERLAY}},.width=64,.height=64},target={.owner=&owner,.width=800,.height=600},peer={.owner=&owner,.width=320,.height=240},bad={.owner=&foreign,.width=64,.height=64},plain={.owner=&owner};owner.surfaces=&overlay;overlay.next=&target;target.next=&peer;peer.next=&plain;
 DDSurface before=overlay;unsigned calls=emits;MUST(surface_UpdateOverlay(&overlay,NULL,NULL,NULL,DDOVER_SHOW,NULL)==DDERR_INVALIDPARAMS);MUST(surface_UpdateOverlay(&overlay,NULL,NULL,NULL,DDOVER_HIDE,NULL)==DDERR_INVALIDPARAMS);MUST(!memcmp(&overlay,&before,sizeof(before))&&emits==calls&&!addrefs&&!releases);
 MUST(surface_UpdateOverlay(&plain,NULL,&target,NULL,DDOVER_SHOW,NULL)==DDERR_NOTAOVERLAYSURFACE);
 MUST(!surface_UpdateOverlay(&overlay,NULL,&target,NULL,DDOVER_SHOW,NULL));MUST(target.refs==1&&emitted.overlay_visible&&emitted.overlay_destination==&target&&emitted.overlay_destination_rect.left==0&&emitted.overlay_destination_rect.top==0&&emitted.overlay_destination_rect.right==800&&emitted.overlay_destination_rect.bottom==600&&emitted.overlay_source_rect.right==64);
 RECT rect={32,16,96,80};MUST(!surface_UpdateOverlay(&overlay,NULL,&target,&rect,DDOVER_SHOW,NULL));LONG x=-1,y=-1;MUST(!surface_GetOverlayPosition(&overlay,&x,&y)&&x==32&&y==16);
 MUST(!surface_UpdateOverlay(&overlay,NULL,&target,NULL,0,NULL));x=y=-1;MUST(!surface_GetOverlayPosition(&overlay,&x,&y)&&!x&&!y&&overlay.overlay_destination_rect.right==800&&overlay.overlay_destination_rect.bottom==600);
 MUST(!surface_UpdateOverlay(&overlay,NULL,NULL,NULL,DDOVER_REFRESHDIRTYRECTS,NULL)&&overlay.overlay_visible&&!overlay.overlay_dirty);
 MUST(!surface_UpdateOverlay(&overlay,NULL,NULL,NULL,DDOVER_HIDE,NULL));x=y=-1;MUST(surface_GetOverlayPosition(&overlay,&x,&y)==DDERR_OVERLAYNOTVISIBLE&&!x&&!y&&target.refs==1);
 MUST(!surface_UpdateOverlay(&overlay,NULL,&peer,NULL,DDOVER_SHOW,NULL)&&!target.refs&&peer.refs==1&&overlay.overlay_destination_rect.right==320&&overlay.overlay_destination_rect.bottom==240);
 before=overlay;calls=emits;MUST(surface_UpdateOverlay(&overlay,NULL,&bad,NULL,DDOVER_SHOW,NULL)==DDERR_INVALIDOBJECT);RECT invalid={-1,0,64,64};MUST(surface_UpdateOverlay(&overlay,&invalid,&peer,NULL,DDOVER_SHOW,NULL)==DDERR_INVALIDRECT);invalid=(RECT){0,0,0,10};MUST(surface_UpdateOverlay(&overlay,NULL,&peer,&invalid,DDOVER_SHOW,NULL)==DDERR_INVALIDRECT);MUST(!memcmp(&overlay,&before,sizeof(before))&&calls==emits&&peer.refs==1&&!target.refs);
 x=y=-1;MUST(surface_GetOverlayPosition(&overlay,NULL,&y)==DDERR_INVALIDPARAMS&&y==-1);MUST(surface_GetOverlayPosition(&plain,&x,&y)==DDERR_NOTAOVERLAYSURFACE&&x==-1&&y==-1);
 fail_emit=TRUE;MUST(surface_UpdateOverlay(&overlay,NULL,&peer,&rect,DDOVER_SHOW,NULL)==DDERR_GENERIC&&peer.refs==1);fail_emit=FALSE;MUST(!surface_UpdateOverlay(&overlay,NULL,&peer,&rect,DDOVER_SHOW,NULL)&&peer.refs==1);surface_Release((void*)&overlay.overlay_destination->vtbl7);overlay.overlay_destination=NULL;
}
int main(void){cases();printf("PASS actual overlay contract C: %u checks, NULL target errors, full destination defaults, hidden coordinates, ownership and emit recovery\n",checks);}
`.replace('PROTOCOL',site+'/glbridge/ddrawproxy/ddraw_protocol.h').replace('FLAGS',flags).replace('METHODS',['surface_UpdateOverlay','surface_GetOverlayPosition'].map(extract).join('\n'));
try{fs.writeFileSync(dir+'/test.c',code);const b=spawnSync('cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
