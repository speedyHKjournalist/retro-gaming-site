"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),source=fs.readFileSync(process.env.DDRAW_METADATA_SOURCE||root+"/ddrawproxy/ddraw_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-resource-metadata-"));
function extract(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return source.slice(m.index,end).replace(/\bWINAPI\b/g,"");}
const thunks=[1,2,3,4].flatMap(view=>[["SetPalette","Unlock"],["SetColorKey","SetOverlayPosition"]].map(([name,next])=>{
 const start=source.indexOf("static HRESULT WINAPI tag##_"+name+"("),end=source.indexOf("static HRESULT WINAPI tag##_"+next+"(",start);assert(start>=0&&end>start);
 return source.slice(start,end).replace(/\\\r?\n/g,"\n").replaceAll("tag##","view"+view).replaceAll("IFACE","IDirectDrawSurface7").replace(/\bVERSION\b/g,String(view)).replace(/\bWINAPI\b/g,"");
})).join("\n");
const prefix=String.raw`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ROOT/ddrawproxy/ddraw_protocol.h"
#include "ROOT/runtime/d9wg_ddraw_color_key.h"
typedef uint32_t DWORD,UINT,ULONG;typedef int32_t LONG,HRESULT,BOOL;typedef DWORD*LPDWORD;
typedef struct{DWORD dwColorSpaceLowValue,dwColorSpaceHighValue;}DDCOLORKEY,*LPDDCOLORKEY;
typedef struct DDSurface DDSurface;
typedef struct{BOOL extended_factory;DDSurface*surfaces;}DDrawObject;
typedef struct{LONG ref;DWORD caps;DDrawObject*owner;}DDPalette,IDirectDrawPalette;typedef IDirectDrawPalette*LPDIRECTDRAWPALETTE;
struct DDSurface{DDrawObject*owner;DDPalette*palette;UINT creation_version,mip_level,cube_face;BOOL indexed,primary,overlay_visible;DWORD priority,color_key_present;DDCOLORKEY color_key[4];DDSurface*next,*overlay_destination;struct{DWORD dwFlags;struct{DWORD dwCaps,dwCaps2;}ddsCaps;DDCOLORKEY ddckCKDestBlt,ddckCKDestOverlay,ddckCKSrcBlt,ddckCKSrcOverlay;}desc;};
typedef DDSurface IDirectDrawSurface7;
static unsigned checks,adds,releases,tables,bindings,key_commands,overlay_commands;
static DDSurface*reentrant_surface;static DDPalette*reentrant_expected;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"resource metadata line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static DDSurface*surface_from_iface(IDirectDrawSurface7*p){return p;}static DDPalette*palette_from_iface(IDirectDrawPalette*p){return p;}
static ULONG test_add(IDirectDrawPalette*p){MUST(p&&p->ref>0);adds++;return++p->ref;}
static ULONG test_release(IDirectDrawPalette*p){MUST(p&&p->ref>0);releases++;ULONG n=--p->ref;if(!n&&reentrant_surface)MUST(reentrant_surface->palette==reentrant_expected);return n;}
static void emit_palette_table(DDPalette*p){MUST(p&&p->ref>1);tables++;}static void emit_surface_palette_binding(DDSurface*s){MUST(s);bindings++;}
static void emit_color_key(DDSurface*s,DWORD k,DDCOLORKEY*key,BOOL present){MUST(s&&k<4&&!!key==!!present);key_commands++;}
static void emit_overlay_state(DDSurface*s){MUST(s&&s->overlay_visible);overlay_commands++;}
`.replaceAll('ROOT',root);
const tests=String.raw`
static void priority_cases(void){
 const struct{DWORD caps,caps2;UINT mip,face;HRESULT hr;}rows[]={
 {DDSCAPS_TEXTURE|DDSCAPS_VIDEOMEMORY,0,0,0,DDERR_INVALIDPARAMS},
 {DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY,0,0,0,DDERR_INVALIDPARAMS},
 {DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY,DDSCAPS2_TEXTUREMANAGE,0,0,DD_OK},
 {DDSCAPS_TEXTURE|DDSCAPS_SYSTEMMEMORY,DDSCAPS2_D3DTEXTUREMANAGE,0,0,DD_OK},
 {DDSCAPS_TEXTURE|DDSCAPS_COMPLEX|DDSCAPS_MIPMAP,DDSCAPS2_TEXTUREMANAGE|DDSCAPS2_CUBEMAP,0,0,DD_OK},
 {DDSCAPS_OFFSCREENPLAIN|DDSCAPS_VIDEOMEMORY,0,0,0,DDERR_INVALIDOBJECT},
 {DDSCAPS_OFFSCREENPLAIN|DDSCAPS_SYSTEMMEMORY,0,0,0,DDERR_INVALIDOBJECT},
 {DDSCAPS_TEXTURE|DDSCAPS_COMPLEX|DDSCAPS_MIPMAP,DDSCAPS2_TEXTUREMANAGE,1,0,DDERR_INVALIDPARAMS},
 {DDSCAPS_TEXTURE|DDSCAPS_COMPLEX|DDSCAPS_MIPMAP,DDSCAPS2_TEXTUREMANAGE|DDSCAPS2_CUBEMAP,0,5,DDERR_INVALIDPARAMS},
 };
 const DWORD values[]={0,1,2,0x7fffffff,0xffffffff};
 for(unsigned round=0;round<64;round++)for(unsigned i=0;i<sizeof(rows)/sizeof(*rows);i++){
  DDrawObject owner={0};DDSurface s;memset(&s,0,sizeof(s));s.owner=&owner;s.desc.ddsCaps.dwCaps=rows[i].caps;s.desc.ddsCaps.dwCaps2=rows[i].caps2;s.mip_level=rows[i].mip;s.cube_face=rows[i].face;
  DWORD out=0xdeadbeef;MUST(surface_GetPriority(&s,&out)==rows[i].hr&&out==(rows[i].hr?0xdeadbeef:0));
  MUST(surface_GetPriority(&s,NULL)==DDERR_INVALIDPARAMS);
  for(unsigned v=0;v<sizeof(values)/sizeof(*values);v++){
   DDSurface before=s;MUST(surface_SetPriority(&s,values[v])==rows[i].hr);if(!rows[i].hr)before.priority=values[v];MUST(!memcmp(&before,&s,sizeof(s)));
   struct{DWORD before,out,after;}got={0x13572468,0xdeadbeef,0x24681357};MUST(surface_GetPriority(&s,&got.out)==rows[i].hr&&got.out==(rows[i].hr?0xdeadbeef:values[v])&&got.before==0x13572468&&got.after==0x24681357);
  }
 }
 MUST(!adds&&!releases&&!tables&&!bindings&&!key_commands&&!overlay_commands);
}
static void missing_palette(void){
 for(unsigned round=0;round<64;round++){
  DDSurface s={0};IDirectDrawPalette*out=(void*)1;MUST(surface_GetPalette(&s,&out)==DDERR_NOPALETTEATTACHED&&!out);MUST(surface_GetPalette(&s,NULL)==DDERR_INVALIDPARAMS);
  DDrawObject owner={0};DDPalette p={.ref=1,.caps=DDPCAPS_8BIT,.owner=&owner};s.owner=&owner;s.indexed=TRUE;s.creation_version=4;s.desc.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE;
  MUST(!surface_SetPalette(&s,&p)&&s.palette==&p&&p.ref==2);out=NULL;MUST(!surface_GetPalette(&s,&out)&&out==&p&&p.ref==3);MUST(test_release(out)==2);MUST(!surface_SetPalette(&s,NULL)&&!s.palette&&p.ref==1);
 }
}
static HRESULT set_palette(UINT view,DDSurface*s,DDPalette*p){switch(view){case 1:return view1_SetPalette(s,p);case 2:return view2_SetPalette(s,p);case 3:return view3_SetPalette(s,p);case 4:return view4_SetPalette(s,p);default:return surface_SetPalette(s,p);}}
static HRESULT set_key(UINT view,DDSurface*s,DWORD flags,DDCOLORKEY*k){switch(view){case 1:return view1_SetColorKey(s,flags,k);case 2:return view2_SetColorKey(s,flags,k);case 3:return view3_SetColorKey(s,flags,k);case 4:return view4_SetColorKey(s,flags,k);default:return surface_SetColorKey(s,flags,k);}}
static void palette_and_key_views(void){
 const UINT views[]={1,2,3,4,7};const DWORD selectors[]={DDCKEY_SRCBLT,DDCKEY_DESTBLT,DDCKEY_SRCOVERLAY,DDCKEY_DESTOVERLAY};
 for(unsigned round=0;round<4;round++)for(unsigned factory=0;factory<3;factory++)for(unsigned v=0;v<5;v++)for(unsigned level=0;level<3;level++)for(unsigned role=0;role<3;role++)for(unsigned rgb=0;rgb<2;rgb++){
  DDrawObject owner={.extended_factory=factory!=0};DDSurface s;memset(&s,0,sizeof(s));s.owner=&owner;owner.surfaces=&s;s.creation_version=factory==2?7:4;s.indexed=!rgb;s.mip_level=level;s.desc.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|(role?DDSCAPS_TEXTURE:DDSCAPS_OFFSCREENPLAIN);s.desc.ddsCaps.dwCaps2=role==2?DDSCAPS2_TEXTUREMANAGE:0;
  DDPalette normal={.ref=1,.caps=DDPCAPS_8BIT,.owner=&owner},alpha={.ref=1,.caps=DDPCAPS_8BIT|DDPCAPS_ALPHA,.owner=&owner};BOOL denied=views[v]==7&&level>0;
  DDSurface before=s;unsigned old_adds=adds,old_releases=releases,old_tables=tables,old_bindings=bindings;
  MUST(set_palette(views[v],&s,NULL)==(denied?DDERR_NOTONMIPMAPSUBLEVEL:DDERR_NOPALETTEATTACHED));MUST(!memcmp(&before,&s,sizeof(s))&&adds==old_adds&&releases==old_releases&&tables==old_tables&&bindings==old_bindings);
  for(unsigned which=0;which<3;which++){
   DDPalette*input=which==0?&normal:which==1?&alpha:NULL,*previous=s.palette;
   HRESULT expected=denied?DDERR_NOTONMIPMAPSUBLEVEL:which==1&&!role?DDERR_INVALIDSURFACETYPE:which<2&&rgb?DDERR_INVALIDPIXELFORMAT:!input&&!previous?DDERR_NOPALETTEATTACHED:DD_OK;
   before=s;LONG nr=normal.ref,ar=alpha.ref;old_adds=adds;old_releases=releases;old_tables=tables;old_bindings=bindings;
   MUST(set_palette(views[v],&s,input)==expected);
   if(expected)MUST(!memcmp(&before,&s,sizeof(s))&&normal.ref==nr&&alpha.ref==ar&&adds==old_adds&&releases==old_releases&&tables==old_tables&&bindings==old_bindings);
   else{MUST(s.palette==input&&normal.ref==1+(input==&normal)&&alpha.ref==1+(input==&alpha));MUST(adds==old_adds+!!input&&releases==old_releases+!!previous);}
   IDirectDrawPalette*out=(void*)1;LONG count=s.palette?s.palette->ref:0;MUST(surface_GetPalette(&s,&out)==(s.palette?DD_OK:DDERR_NOPALETTEATTACHED)&&out==s.palette);if(out)MUST(out->ref==count+1&&test_release(out)==(ULONG)count);
   if(!expected&&input){old_adds=adds;old_releases=releases;MUST(!set_palette(views[v],&s,input)&&s.palette==input&&input->ref==2&&adds==old_adds+1&&releases==old_releases+1);}
  }
  MUST(!s.palette&&normal.ref==1&&alpha.ref==1);
  for(unsigned k=0;k<4;k++){
   DDCOLORKEY key={0x22,0x22},saved=key;before=s;unsigned old_keys=key_commands,old_overlays=overlay_commands;
   MUST(set_key(views[v],&s,selectors[k],&key)==(denied?DDERR_NOTONMIPMAPSUBLEVEL:DD_OK)&&!memcmp(&key,&saved,sizeof(key)));
   if(denied)MUST(!memcmp(&s,&before,sizeof(s))&&key_commands==old_keys&&overlay_commands==old_overlays);
   DDCOLORKEY got={0xdeadbeef,0xdeadbeef};MUST(surface_GetColorKey(&s,selectors[k],&got)==(denied?DDERR_NOCOLORKEY:DD_OK));MUST(got.dwColorSpaceLowValue==(denied?0xdeadbeef:0x22)&&got.dwColorSpaceHighValue==got.dwColorSpaceLowValue);
   MUST(set_key(views[v],&s,selectors[k],NULL)==(denied?DDERR_NOTONMIPMAPSUBLEVEL:DD_OK));
  }
  // A cube face at mip zero is not a mip sublevel for palette or key setters.
  s.mip_level=0;s.cube_face=5;s.indexed=TRUE;s.desc.ddsCaps.dwCaps|=DDSCAPS_TEXTURE;s.desc.ddsCaps.dwCaps2|=DDSCAPS2_CUBEMAP;DDCOLORKEY key={0x22,0x22};
  MUST(!set_palette(views[v],&s,&normal)&&!set_palette(views[v],&s,NULL));MUST(!set_key(views[v],&s,DDCKEY_SRCBLT,&key)&&!set_key(views[v],&s,DDCKEY_SRCBLT,NULL));MUST(normal.ref==1&&alpha.ref==1);
 }
}
static void replacement_order(void){
 DDrawObject owner={0};DDSurface s={.owner=&owner,.indexed=TRUE};DDPalette p={.ref=1,.caps=DDPCAPS_8BIT},q=p;s.desc.ddsCaps.dwCaps=DDSCAPS_TEXTURE;
 MUST(!surface_SetPalette(&s,&p)&&p.ref==2);MUST(test_release(&p)==1);reentrant_surface=&s;reentrant_expected=&q;
 MUST(!surface_SetPalette(&s,&q)&&p.ref==0&&q.ref==2&&s.palette==&q);MUST(test_release(&q)==1);reentrant_expected=NULL;MUST(!surface_SetPalette(&s,NULL)&&q.ref==0&&!s.palette);reentrant_surface=NULL;
 MUST(surface_SetPalette(&s,NULL)==DDERR_NOPALETTEATTACHED);
}
int main(void){(void)&surface_SetColorKey;(void)&surface_GetColorKey;priority_cases();missing_palette();palette_and_key_views();replacement_order();printf("PASS actual SDK resource metadata C: %u checks, managed root priorities, per-call-version palette/key mip rules through real legacy thunks, cube faces, role/error isolation, references and reentrant replacement\n",checks);return 0;}
`;
try{
 const file=dir+'/sdk.c';fs.writeFileSync(file,'#define COBJMACROS\n#include <ddraw.h>\n#include "'+root+'/ddrawproxy/ddraw_protocol.h"\n#undef IDirectDrawPalette_AddRef\n#undef IDirectDrawPalette_Release\n#define IDirectDrawPalette_AddRef(p) test_add(p)\n#define IDirectDrawPalette_Release(p) test_release(p)\n#define DDWG_TRACE_ERROR(h) (h)\n#define DD_VERSION_7 7u\n#define SURFACE7(p) (p)\nstatic int metadata_begin;\n'+['surface_color_key_kind','surface_description_color_key','surface_GetColorKey','surface_SetColorKeyForVersion','surface_SetColorKey','surface_GetPalette','surface_SetPaletteForVersion','surface_SetPalette','surface_priority_valid','surface_SetPriority','surface_GetPriority'].map(extract).join('\n')+thunks+tests+'\nstatic int metadata_end;');
 const pp=spawnSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-P',file],{encoding:'utf8',maxBuffer:16<<20});assert.equal(pp.status,0,pp.stderr);const body=pp.stdout.split('static int metadata_begin;')[1].split('static int metadata_end;')[0];fs.writeFileSync(dir+'/test.c',prefix+body);
 const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
