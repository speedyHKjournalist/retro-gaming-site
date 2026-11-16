"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(process.env.DDRAW_ATTACHMENT_SOURCE||path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8");
const graph=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../../../v86/tests/glbridge/fixtures/ddraw_native_attachment_graphs.json')));
for(const[f,h]of[[graph.source,graph.sourceSha256],['glbridge/sample/ddraw_attachment_reference.c',graph.includedSourceSha256]])assert.equal(require('node:crypto').createHash('sha256').update(fs.readFileSync(path.resolve(__dirname,'../..',f))).digest('hex'),h);
const start=source.indexOf("static HRESULT WINAPI surface_GetAttachedSurface("),end=source.indexOf("\nstatic HRESULT WINAPI surface_GetBltStatus",start);assert(start>=0&&end>start);
function extract(name){const re=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{");const match=re.exec(source);assert(match,name);let e=source.indexOf("{",match.index)+1,d=1;for(;d;e++){if(source[e]==="{")d++;if(source[e]==="}")d--;}return source.slice(match.index,e);}
const code=`#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,UINT;typedef int32_t HRESULT;typedef int BOOL;typedef void DDrawObject;typedef struct{DWORD dwCaps,dwCaps2,dwCaps3,dwCaps4;}DDSCAPS2,*LPDDSCAPS2;
typedef struct{DWORD dwSize,dwFlags,dwWidth,dwHeight,dwMipMapCount;DDSCAPS2 ddsCaps;}DDSURFACEDESC2;
typedef struct DDSurface{void*vtbl7;BOOL upload_pending;DDSURFACEDESC2 desc;void*owner,*storage;UINT width,height;struct DDSurface*mip_next,*attached_depth,*flip_next,*cube_faces[6];UINT mip_level,cube_face,refs;}DDSurface,IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;
#define TRUE 1
#define FALSE 0
#define DDSD_CAPS 1u
#define DDSD_WIDTH 4u
#define DDSD_HEIGHT 2u
#define DDSD_PIXELFORMAT 0x1000u
#define DDSD_MIPMAPCOUNT 0x20000u
#define DDSD_BACKBUFFERCOUNT 0x20u
#define DDSCAPS_PRIMARYSURFACE 0x200u
#define DDSCAPS_FRONTBUFFER 0x20u
#define DDSCAPS_BACKBUFFER 4u
#define DDSCAPS_FLIP 16u
#define DDSCAPS_VISIBLE 0x8000u
#define WINAPI
#define DD_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_NOTFOUND (-2)
#define FAILED(h) ((HRESULT)(h)<0)
#define DDERR_SURFACELOST ((HRESULT)0x887601c2u)
#define DDWG_TRACE_ERROR(h) (h)
#define DDSCAPS_COMPLEX 8u
#define DDSCAPS_TEXTURE 0x1000u
#define DDSCAPS_MIPMAP 0x400000u
#define DDSCAPS_ZBUFFER 0x20000u
#define DDSCAPS2_CUBEMAP 0x200u
#define DDSCAPS2_CUBEMAP_POSITIVEX 0x400u
#define DDSCAPS2_CUBEMAP_NEGATIVEX 0x800u
#define DDSCAPS2_CUBEMAP_POSITIVEY 0x1000u
#define DDSCAPS2_CUBEMAP_NEGATIVEY 0x2000u
#define DDSCAPS2_CUBEMAP_POSITIVEZ 0x4000u
#define DDSCAPS2_CUBEMAP_NEGATIVEZ 0x8000u
#define DDSCAPS2_CUBEMAP_ALLFACES 0xfc00u
#define DDSCAPS2_MIPMAPSUBLEVEL 0x10000u
static unsigned checks;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"attachment query line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static HRESULT surface_IsLost(void*p){(void)p;return 0;}
static DDSurface*surface_from_iface(DDSurface*p){return p;}
static void IDirectDrawSurface7_AddRef(DDSurface*p){MUST(p);p->refs++;}
static DDSurface pool[64];static unsigned allocated;
static DDSurface*surface_allocate(void*owner,const DDSURFACEDESC2*desc,unsigned format,UINT width,UINT height,DWORD caps,void*storage,UINT level,UINT face){MUST(format==32&&allocated<64&&caps==desc->ddsCaps.dwCaps);DDSurface*p=&pool[allocated++];memset(p,0,sizeof(*p));p->owner=owner;p->storage=storage?storage:(void*)(uintptr_t)(0x1000u+allocated*0x100u);p->desc=*desc;p->width=width;p->height=height;p->mip_level=level;p->cube_face=face;p->refs=1;return p;}
${extract("surface_build_mip_chain")}
static BOOL emit_surface_storage_create(DDSurface*s,UINT levels,BOOL cube){MUST(s&&levels&&cube);return TRUE;}
static UINT surface_Release(DDSurface*s){(void)s;MUST(FALSE);return 0;}
static void surface_destroy(DDSurface*s){(void)s;MUST(FALSE);}
${extract("surface_create_complex_texture")}
${source.slice(start,end)}
static void constructed_mips(void){DDSurface root={.width=128,.height=64,.owner=(void*)1,.storage=(void*)2};DDSURFACEDESC2 desc={.dwFlags=DDSD_BACKBUFFERCOUNT,.ddsCaps={.dwCaps=DDSCAPS_TEXTURE|DDSCAPS_COMPLEX|DDSCAPS_MIPMAP}};allocated=0;MUST(surface_build_mip_chain(&root,&desc,32,8,desc.ddsCaps.dwCaps,0)&&allocated==7);DDSurface*p=&root;DDSCAPS2 caps={.dwCaps=DDSCAPS_COMPLEX};for(unsigned level=1;level<8;level++){DDSurface*next=NULL;MUST(surface_GetAttachedSurface(p,&caps,&next)==DD_OK&&next==&pool[level-1]);MUST(next->desc.ddsCaps.dwCaps&DDSCAPS_COMPLEX);MUST(next->mip_level==level&&next->width==(128u>>level)&&next->height==(level<7?64u>>level:1));MUST(!(next->desc.dwFlags&DDSD_BACKBUFFERCOUNT)&&next->desc.dwMipMapCount==8-level);p=next;}MUST(!p->mip_next);}

static void mips(void){DDSurface levels[4]={0};DWORD bits[]={DDSCAPS_TEXTURE,DDSCAPS_COMPLEX,DDSCAPS_MIPMAP};for(unsigned i=0;i<4;i++){levels[i].desc.ddsCaps.dwCaps=bits[0]|bits[1]|bits[2];levels[i].desc.ddsCaps.dwCaps2=i?DDSCAPS2_MIPMAPSUBLEVEL:0;levels[i].refs=1;levels[i].mip_next=i<3?&levels[i+1]:NULL;}
 for(unsigned i=0;i<4;i++)for(unsigned subset=0;subset<8;subset++)for(unsigned sublevel=0;sublevel<2;sublevel++){
  DDSCAPS2 caps={0};for(unsigned j=0;j<3;j++)if(subset&(1u<<j))caps.dwCaps|=bits[j];caps.dwCaps2=sublevel?DDSCAPS2_MIPMAPSUBLEVEL:DDSCAPS2_CUBEMAP_ALLFACES;DDSurface*out=(void*)1;
  HRESULT hr=surface_GetAttachedSurface(&levels[i],&caps,&out);MUST(hr==(i<3?DD_OK:DDERR_NOTFOUND));if(i<3){MUST(out==&levels[i+1]&&out->refs==2);--out->refs;}else MUST(!out);
  caps.dwCaps|=0x80000000u;out=(void*)1;MUST(surface_GetAttachedSurface(&levels[i],&caps,&out)==DDERR_NOTFOUND&&!out);for(unsigned j=0;j<4;j++)MUST(levels[j].refs==1);
 }
 DDSCAPS2 caps={0};DDSurface*out=(void*)1;MUST(surface_GetAttachedSurface(levels,NULL,&out)==DDERR_INVALIDPARAMS);MUST(surface_GetAttachedSurface(levels,&caps,NULL)==DDERR_INVALIDPARAMS);
}
static void other_attachments(void){DDSurface root={0},depth={.refs=1},back={.refs=1};DDSCAPS2 caps={.dwCaps=DDSCAPS_ZBUFFER};root.attached_depth=&depth;DDSurface*out=NULL;MUST(surface_GetAttachedSurface(&root,&caps,&out)==DD_OK&&out==&depth&&depth.refs==2);root.attached_depth=NULL;caps.dwCaps=4;root.flip_next=&back;back.flip_next=&root;back.desc.ddsCaps.dwCaps=4;MUST(surface_GetAttachedSurface(&root,&caps,&out)==DD_OK&&out==&back&&back.refs==2);}
static void native_flip_queries(void){const struct{unsigned version,reverse,node,caps;HRESULT hr;int found;}rows[]={${graph.rows.filter(r=>r.action==='complex'||r.action==='reverse').map(r=>'{'+[r.view,r.action==='reverse'?1:0,r.node,r.selector,r.hr?'DDERR_NOTFOUND':'DD_OK',r.found].join(',')+'}').join(',')}};for(unsigned repeat=0;repeat<16;repeat++)for(unsigned i=0;i<sizeof(rows)/sizeof(*rows);i++){DDSurface nodes[4]={0};for(unsigned j=0;j<4;j++)nodes[j].refs=1;const unsigned v=rows[i].version;unsigned count=rows[i].reverse?2:4;if(rows[i].reverse){nodes[0].desc.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;nodes[1].desc.ddsCaps.dwCaps=0x40u;if(v!=7){nodes[0].desc.ddsCaps.dwCaps|=DDSCAPS_FLIP|DDSCAPS_BACKBUFFER;nodes[1].desc.ddsCaps.dwCaps|=DDSCAPS_FLIP|DDSCAPS_FRONTBUFFER;nodes[0].flip_next=&nodes[1];nodes[1].flip_next=&nodes[0];}}else{nodes[0].desc.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE|DDSCAPS_FRONTBUFFER|DDSCAPS_FLIP|DDSCAPS_COMPLEX;nodes[1].desc.ddsCaps.dwCaps=nodes[2].desc.ddsCaps.dwCaps=DDSCAPS_FLIP|DDSCAPS_COMPLEX;nodes[3].desc.ddsCaps.dwCaps=0x2000u;nodes[1].flip_next=&nodes[2];nodes[2].flip_next=&nodes[0];if(v==7){nodes[0].flip_next=&nodes[1];nodes[1].desc.ddsCaps.dwCaps|=DDSCAPS_BACKBUFFER;}else{nodes[0].flip_next=&nodes[3];nodes[3].flip_next=&nodes[1];nodes[3].desc.ddsCaps.dwCaps|=DDSCAPS_FLIP|DDSCAPS_BACKBUFFER;}}DDSCAPS2 caps={.dwCaps=rows[i].caps};DDSurface*out=(void*)1;HRESULT hr=surface_GetAttachedSurface(&nodes[rows[i].node],&caps,&out);MUST(hr==rows[i].hr);if(!hr){MUST(rows[i].found>=0&&out==&nodes[rows[i].found]&&out->refs==2);--out->refs;}else MUST(!out);for(unsigned j=0;j<count;j++)MUST(nodes[j].refs==1);}}
static void cube_queries(void){
 const DWORD faces[]={DDSCAPS2_CUBEMAP_POSITIVEX,DDSCAPS2_CUBEMAP_NEGATIVEX,DDSCAPS2_CUBEMAP_POSITIVEY,DDSCAPS2_CUBEMAP_NEGATIVEY,DDSCAPS2_CUBEMAP_POSITIVEZ,DDSCAPS2_CUBEMAP_NEGATIVEZ};
 const DWORD role=DDSCAPS_TEXTURE|DDSCAPS_COMPLEX|0x800u;
 for(unsigned levels=1;levels<=3;levels+=2){
  DDSURFACEDESC2 request={.dwSize=sizeof(request),.dwFlags=DDSD_CAPS|DDSD_WIDTH|DDSD_HEIGHT,.dwWidth=16,.dwHeight=16,.ddsCaps={.dwCaps=role|(levels>1?DDSCAPS_MIPMAP:0),.dwCaps2=DDSCAPS2_CUBEMAP|DDSCAPS2_CUBEMAP_ALLFACES}};
  allocated=0;DDSurface*root=surface_create_complex_texture((void*)1,&request,32,16,16,request.ddsCaps.dwCaps,levels,TRUE);MUST(root&&allocated==6*levels&&root->desc.ddsCaps.dwCaps2==(DDSCAPS2_CUBEMAP|faces[0])&&root->cube_faces[0]==root);
  for(unsigned face=0;face<6;face++){
   DDSurface*source=root->cube_faces[face];MUST(source&&source->cube_face==face&&source->mip_level==0&&source->desc.ddsCaps.dwCaps2==(DDSCAPS2_CUBEMAP|faces[face]));
   for(unsigned wanted=0;wanted<6;wanted++)for(unsigned selector=0;selector<3;selector++){
    DDSCAPS2 caps={.dwCaps=selector==0?DDSCAPS_COMPLEX:selector==1?DDSCAPS_TEXTURE:0,.dwCaps2=faces[wanted]};DDSurface*out=(void*)1;BOOL mip=face==wanted&&levels>1,other=face==0&&wanted!=0;DDSurface*expected=mip?source->mip_next:other?root->cube_faces[wanted]:NULL;unsigned before=expected?expected->refs:0;
    MUST(surface_GetAttachedSurface(source,&caps,&out)==(expected?DD_OK:DDERR_NOTFOUND));MUST(out==expected&&out!=source);if(out){MUST(out->refs==before+1&&out->width==(mip?8:16));out->refs--;}
    caps.dwCaps|=0x80000000u;out=(void*)1;MUST(surface_GetAttachedSurface(source,&caps,&out)==DDERR_NOTFOUND&&!out);
   }
   DDSurface*current=source;
   for(unsigned step=0;step<levels;step++){
    DDSCAPS2 caps={.dwCaps=DDSCAPS_COMPLEX,.dwCaps2=faces[face]};DDSurface*out=(void*)1;BOOL success=step+1<levels;DDSurface*expected=success?current->mip_next:NULL;unsigned before=expected?expected->refs:0;
    MUST(surface_GetAttachedSurface(current,&caps,&out)==(success?DD_OK:DDERR_NOTFOUND)&&out==expected&&out!=current);if(!success)break;MUST(out->width==(8u>>step)&&out->mip_level==step+1&&out->refs==before+1);out->refs--;current=out;
   }
   // A same-face sublevel request follows that face's mip edge.
   DDSCAPS2 caps={.dwCaps=DDSCAPS_TEXTURE,.dwCaps2=faces[face]|DDSCAPS2_MIPMAPSUBLEVEL};DDSurface*out=NULL;MUST(surface_GetAttachedSurface(source,&caps,&out)==(levels>1?DD_OK:DDERR_NOTFOUND));if(out){MUST(out==source->mip_next);out->refs--;}
  }
  for(unsigned i=0;i<allocated;i++)MUST(pool[i].refs==1);
 }
}
int main(void){native_flip_queries();for(unsigned i=0;i<32;i++){constructed_mips();mips();other_attachments();cube_queries();}printf("PASS actual DirectDraw attachment query C: %u checks, actual mip/cube construction, native one/three-level face query matrix, positive-X descriptors, no self edges, per-face traversal and leaves, caps filters, non-cube caps2 and retained depth/flip paths\\n",checks);return 0;}
`;
const dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-query-"));
try{const file=dir+"/query.c",binary=dir+"/query";fs.writeFileSync(file,code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-O1","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",file,"-o",binary],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
