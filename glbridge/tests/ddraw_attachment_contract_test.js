'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),{createHash}=require('node:crypto');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_ATTACHMENT_SOURCE||root+'/ddrawproxy/ddraw_proxy.c','utf8');
const native=JSON.parse(fs.readFileSync(root+'/../../v86/tests/glbridge/fixtures/ddraw_native_attachments.json'));
assert.equal(createHash('sha256').update(fs.readFileSync(root+'/../'+native.source)).digest('hex'),native.sourceSha256);
const graph=JSON.parse(fs.readFileSync(root+'/../../v86/tests/glbridge/fixtures/ddraw_native_attachment_graphs.json'));
assert.equal(createHash('sha256').update(fs.readFileSync(root+'/../'+graph.source)).digest('hex'),graph.sourceSha256);
assert.equal(native.sourceSha256,graph.includedSourceSha256);
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-attachment-contract-'));
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let e=source.indexOf('{',m.index)+1,d=1;for(;d;e++){if(source[e]==='{')d++;if(source[e]==='}')d--;}return source.slice(m.index,e);}
function thunk(name,next,v){let t=source.slice(source.indexOf('static HRESULT WINAPI tag##_'+name+'('),source.indexOf('static HRESULT WINAPI tag##_'+next+'('));assert(t);return t.replace(/\\\n/g,'\n').replaceAll('tag##','v'+v).replace(/\bIFACE\b/g,'IDirectDrawSurface7').replace(/\bDESC\b/g,'DDSURFACEDESC2').replace(/\bENUMCB\b/g,'LPDDENUMSURFACESCALLBACK7').replace(/\bCBIFACE\b/g,'IDirectDrawSurface7*').replace(/\bCAPS\b/g,'DDSCAPS2').replaceAll('SURFACE7(iface)','surface_from_any(iface, NULL)').replace(/\bVERSION\b/g,String(v));}
try{
 const names=['surface_private_addref','surface_private_release','surface_public_addref','surface_public_release','surface_transfer_public_ref','surface_factory_view','surface_flip_contains','surface_manual_flip_attachment','surface_flip_backbuffer_count','surface_AddAttachedSurfaceForVersion','surface_AddAttachedSurface','surface_DeleteAttachedSurfaceForVersion','surface_DeleteAttachedSurface','surface_GetAttachedSurface','surface_EnumAttachedSurfaces'];
 const body=names.map(extract).join('\n')+'\n'+[1,2,3,4,7].flatMap(v=>[thunk('AddAttachedSurface','AddOverlayDirtyRect',v),thunk('DeleteAttachedSurface','EnumAttachedSurfaces',v),thunk('GetAttachedSurface','GetBltStatus',v),thunk('EnumAttachedSurfaces','EnumOverlayZOrders',v)]).join('\n');
 fs.writeFileSync(dir+'/sdk.c',`#define COBJMACROS\n#include <ddraw.h>\n#undef WINAPI\n#define WINAPI\n#define DD_VERSION_1 1u\n#define DD_VERSION_2 2u\n#define DD_VERSION_3 3u\n#define DD_VERSION_4 4u\n#define DD_VERSION_7 7u\n#define DDWG_TRACE_ERROR(h) (h)\n#undef ZeroMemory\n#define ZeroMemory(p,n) memset(p,0,n)\n#undef IDirectDrawSurface7_AddRef\n#define IDirectDrawSurface7_AddRef(p) surface_public_addref(surface_from_any(p,NULL),7)\n#undef IDirectDrawSurface7_Release\n#define IDirectDrawSurface7_Release(p) surface_public_release(surface_from_any(p,NULL),7)\nstatic int methods_begin;\n${body}\nstatic int methods_end;\n`);
 const pp=spawnSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-P',dir+'/sdk.c'],{encoding:'utf8',maxBuffer:16<<20});assert.equal(pp.status,0,pp.stderr);
 const text=pp.stdout,m=/}\s*DDSCAPS2[ ,;]/.exec(text);assert(m);const caps=text.slice(text.lastIndexOf('typedef struct',m.index),text.indexOf(';',m.index)+1);
 const rows=native.rows.map(r=>'{'+[['delete','duplicate','query','retire','size'].indexOf(r.action),r.add_view,r.other_view,r.hr_add+'u',r.hr_op+'u',r.depth_size,r.returned_view,'{'+r.refs.join(',')+'}'].join(',')+'}').join(',');
 const enums=graph.rows.filter(r=>r.action==='enum').map(r=>'{'+[r.view,r.hr+'u',r.found,r.count,r.callback_refs].join(',')+'}').join(',');
 const code=String.raw`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD,UINT,ULONG;typedef int32_t LONG,HRESULT;typedef int BOOL;
CAPS_TYPE
typedef struct{DWORD dwSize,dwFlags,dwBackBufferCount;DDSCAPS2 ddsCaps;}DDSURFACEDESC2;
typedef struct{unsigned id;DWORD cooperative_flags;}DDrawObject;
typedef struct DDSurface{void*vtbl7,*vtbl4,*vtbl3,*vtbl2,*vtbl1;LONG ref,public_refs[8];unsigned creation_version;BOOL implicit,owns_flip_chain,flip_root_ref,front_of_chain,primary;unsigned flip_root_ref_version,attachment_version,attached_depth_version,attached_depth_namespace;DWORD attach_saved_caps;UINT width,height,format,cube_face;DDrawObject*owner;DDSURFACEDESC2 desc;struct DDSurface*flip_next,*mip_next,*cube_faces[6],*attached_depth;}DDSurface,IDirectDrawSurface7,*LPDIRECTDRAWSURFACE7;
typedef void*LPVOID;typedef HRESULT(*LPDDENUMSURFACESCALLBACK7)(IDirectDrawSurface7*,DDSURFACEDESC2*,void*);
#define TRUE 1
#define FALSE 0
#define FAILED(h) ((h)<0)
#define SUCCEEDED(h) ((h)>=0)
static unsigned checks,destroyed;static DDSurface*known[2];static const unsigned versions[]={1,2,3,4,7};
#define MUST(x) do{++checks;if(!(x)){fprintf(stderr,"attachment contract line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static LONG _InterlockedIncrement(LONG*p){return ++*p;}static LONG _InterlockedDecrement(LONG*p){return --*p;}
static void*surface_interface(DDSurface*s,unsigned v){return v==1?&s->vtbl1:v==2?&s->vtbl2:v==3?&s->vtbl3:v==4?&s->vtbl4:&s->vtbl7;}
static DDSurface*surface_from_any(void*p,unsigned*v){for(unsigned i=0;i<2;i++)for(unsigned j=0;j<5;j++)if(p==surface_interface(known[i],versions[j])){if(v)*v=versions[j];return known[i];}return NULL;}
static DDSurface*surface_from_iface(DDSurface*p){MUST(p==known[0]||p==known[1]);return p;}
static void surface_destroy(DDSurface*s);
static void d3d7_refresh_render_target(DDrawObject*d,DDSurface*s){MUST(d&&s);}
static HRESULT surface_IsLost(void*p){MUST(p);return 0;}
static void legacy_describe(DDSurface*s,DDSURFACEDESC2*d){*d=s->desc;}
METHODS
static void surface_destroy(DDSurface*s){MUST(s->ref==0);++destroyed;if(s->attached_depth){DDSurface*d=s->attached_depth;s->attached_depth=NULL;surface_public_release(d,s->attached_depth_version);}}
typedef HRESULT(*Add)(IDirectDrawSurface7*,IDirectDrawSurface7*);typedef HRESULT(*Del)(IDirectDrawSurface7*,DWORD,IDirectDrawSurface7*);typedef HRESULT(*Get)(IDirectDrawSurface7*,DDSCAPS2*,IDirectDrawSurface7**);
static const Add adds[]={v1_AddAttachedSurface,v2_AddAttachedSurface,v3_AddAttachedSurface,v4_AddAttachedSurface,surface_AddAttachedSurface};
static const Del dels[]={v1_DeleteAttachedSurface,v2_DeleteAttachedSurface,v3_DeleteAttachedSurface,v4_DeleteAttachedSurface,surface_DeleteAttachedSurface};
static const Get getters[]={v1_GetAttachedSurface,v2_GetAttachedSurface,v3_GetAttachedSurface,v4_GetAttachedSurface,v7_GetAttachedSurface};
typedef HRESULT(*Enum)(IDirectDrawSurface7*,void*,LPDDENUMSURFACESCALLBACK7);
static const Enum enumerators[]={v1_EnumAttachedSurfaces,v2_EnumAttachedSurfaces,v3_EnumAttachedSurfaces,v4_EnumAttachedSurfaces,surface_EnumAttachedSurfaces};
static unsigned index_of(unsigned v){for(unsigned i=0;i<5;i++)if(versions[i]==v)return i;MUST(0);return 0;}
static const struct Row{unsigned action,a,b;DWORD first,op;UINT size,returned,refs[5];}rows[]={NATIVE_ROWS};
static const struct EnumRow{unsigned view;DWORD hr;unsigned found,count,refs;}enum_rows[]={ENUM_ROWS};
typedef struct{DDSurface*depth;unsigned found,count,refs;}EnumContext;
static HRESULT collect(IDirectDrawSurface7*p,DDSURFACEDESC2*desc,void*data){EnumContext*c=data;unsigned v=0;MUST(desc&&surface_from_any(p,&v)==c->depth);c->found=v;c->refs=c->depth->public_refs[v];++c->count;surface_public_release(c->depth,v);return 1;}
int main(void){
 (void)&v7_AddAttachedSurface;(void)&v7_DeleteAttachedSurface;(void)&v7_EnumAttachedSurfaces;
 for(unsigned repeat=0;repeat<16;repeat++)for(unsigned i=0;i<sizeof(rows)/sizeof(*rows);i++){
  const struct Row*r=&rows[i];DDrawObject owner={.id=1};DDSurface color={.ref=5,.creation_version=4,.width=64,.height=64,.owner=&owner},depth=color;
  depth.width=depth.height=r->size;depth.desc.ddsCaps.dwCaps=0x20000u;
  known[0]=&color;known[1]=&depth;for(unsigned j=0;j<5;j++){color.public_refs[versions[j]]=1;depth.public_refs[versions[j]]=1;}
  unsigned a=index_of(r->a),b=index_of(r->b);IDirectDrawSurface7*c=surface_interface(&color,r->a),*d=surface_interface(&depth,r->a);
  HRESULT first=adds[a](c,d),op=first;MUST((DWORD)first==r->first);unsigned returned=0;
  if(r->action==0)op=dels[b](surface_interface(&color,r->b),0,surface_interface(&depth,r->b));
  if(r->action==1)op=adds[b](surface_interface(&color,r->b),surface_interface(&depth,r->b));
  if(r->action==2){DDSCAPS2 caps={.dwCaps=0x20000u};IDirectDrawSurface7*out=NULL;op=getters[b](surface_interface(&color,r->b),&caps,&out);for(unsigned v=0;v<5;v++)if(out==surface_interface(&depth,versions[v]))returned=versions[v];if(out){unsigned v=0;MUST(surface_from_any(out,&v)==&depth);surface_public_release(&depth,v);}}
  if(r->action==3)op=(HRESULT)surface_public_release(&color,r->a);
  MUST((DWORD)op==r->op&&returned==r->returned);
  // Add orthogonal dimension perturbations to the captured square failures.
  // These are policy boundary cases, not extra native observations.
  if(r->action==4&&r->size!=64){depth.height=64;MUST((DWORD)adds[a](c,d)==r->first);depth.width=64;depth.height=r->size;MUST((DWORD)adds[a](c,d)==r->first);}

  for(unsigned j=0;j<5;j++)MUST((UINT)depth.public_refs[versions[j]]==r->refs[j]);
  unsigned old=destroyed;
  for(unsigned j=0;j<5;j++)if(color.public_refs[versions[j]])surface_public_release(&color,versions[j]);
  for(unsigned j=0;j<5;j++){MUST(depth.public_refs[versions[j]]==1);surface_public_release(&depth,versions[j]);}
  MUST(!color.ref&&!depth.ref&&destroyed==old+2);
 }
 for(unsigned repeat=0;repeat<16;repeat++)for(unsigned i=0;i<sizeof(enum_rows)/sizeof(*enum_rows);i++){
  const struct EnumRow*r=&enum_rows[i];DDrawObject owner={.id=1};DDSurface color={.ref=5,.creation_version=4,.width=64,.height=64,.owner=&owner},depth=color;
  depth.desc.ddsCaps.dwCaps=0x20000u;known[0]=&color;known[1]=&depth;for(unsigned j=0;j<5;j++){color.public_refs[versions[j]]=1;depth.public_refs[versions[j]]=1;}
  MUST(!adds[3](surface_interface(&color,4),surface_interface(&depth,4)));EnumContext c={.depth=&depth};
  HRESULT hr=enumerators[index_of(r->view)](surface_interface(&color,r->view),&c,collect);
  MUST((DWORD)hr==r->hr&&c.found==r->found&&c.count==r->count&&c.refs==r->refs);
  for(unsigned j=0;j<5;j++)MUST(depth.public_refs[versions[j]]==(versions[j]==4?2:1));
  for(unsigned j=0;j<5;j++)surface_public_release(&color,versions[j]);for(unsigned j=0;j<5;j++)surface_public_release(&depth,versions[j]);MUST(!color.ref&&!depth.ref);
 }
 printf("PASS actual attachment contract C: %u checks, 95 native rows and 5 native enumeration records, five public interface namespaces, duplicate/delete/getter identity, size rejection, reference preservation and complete cleanup\n",checks);return 0;
}
`.replace('CAPS_TYPE',caps).replace('METHODS',text.split('static int methods_begin;')[1].split('static int methods_end;')[0]).replace('NATIVE_ROWS',rows).replace('ENUM_ROWS',enums);
 fs.writeFileSync(dir+'/test.c',code);
 const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);
 const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
