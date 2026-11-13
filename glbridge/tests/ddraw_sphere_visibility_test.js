'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),{createHash}=require('node:crypto');
const root=path.resolve(__dirname,'..'),core=path.resolve(root,'../../v86'),sha=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const fixture=JSON.parse(fs.readFileSync(core+'/tests/glbridge/fixtures/ddraw_native_rgb_spheres.json'));
assert.equal(sha(core+'/'+fixture.source),fixture.sourceSha256);
const seven=fs.readFileSync(process.env.DDRAW_SPHERE_SEVEN_SOURCE||root+'/ddrawproxy/d3d7_proxy.inc','utf8'),legacy=fs.readFileSync(process.env.DDRAW_SPHERE_LEGACY_SOURCE||root+'/ddrawproxy/d3d_legacy_proxy.inc','utf8');
function extract(source,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let e=source.indexOf('{',m.index)+1,d=1;for(;d;e++){if(source[e]==='{')d++;if(source[e]==='}')d--;}return source.slice(m.index,e);}
const manifest=JSON.parse(fs.readFileSync(core+'/tests/glbridge/wine/manifest.json'));assert.equal(manifest.commit,fixture.pinnedWineCommit);
function original(version){const file='ddraw_ddraw'+version+'.c',source=fs.readFileSync(core+'/tests/glbridge/wine/'+file,'utf8');assert.equal(sha(core+'/tests/glbridge/wine/'+file),manifest.files.find(f=>f.path===file).sha256);const fn=extract(source,'test_compute_sphere_visibility');let table=fn.slice(fn.indexOf('{')+1,fn.indexOf(version===4?'IDirect3DViewport3 *viewport;':'IDirect3DDevice7 *device;'));
 // Keep every numeric case and expected word, including upstream todo rows.
 // Only the Wine-platform annotation is absent from this Windows-only table.
 if(version===4)table=table.replace(/\s*BOOL todo;/,'').replace(/,\s*TRUE(?=\s*})/g,'');
 return 'static void original'+version+'(void){'+table+'D3D7Device d={0};d.transforms[1]=identity;DWORD output[3];'+(version===7?'memcpy(d.clip_planes[0],clip_plane,sizeof(clip_plane));':'')+'for(unsigned i=0;i<sizeof(tests)/sizeof(*tests);i++){d.transforms[2]=*tests[i].view;d.transforms[3]=*tests[i].proj;'+(version===7?'d.render_states[152]=tests[i].enable_planes;':'')+'MUST(!d3d'+(version===4?3:7)+'_device_ComputeSphereVisibility(&d,tests[i].center,tests[i].radius,tests[i].sphere_count,0,output));for(unsigned j=0;j<tests[i].sphere_count;j++)MUST(output[j]==tests[i].expected[j]);}}';}
const prefix=String.raw`
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "MATH_HEADER"
typedef uint8_t BYTE;typedef uint32_t DWORD;typedef int32_t HRESULT;typedef int BOOL;typedef float D3DVALUE;
typedef struct {union{float x;};union{float y;};union{float z;};}D3DVECTOR;
typedef struct {float _11,_12,_13,_14,_21,_22,_23,_24,_31,_32,_33,_34,_41,_42,_43,_44;}D3DMATRIX;
typedef struct {D3DMATRIX transforms[4];DWORD render_states[153];float clip_planes[6][4];}D3D7Device,IDirect3DDevice7,IDirect3DDevice3;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define D3D_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define D3DERR_INVALIDMATRIX (-2)
#define DDWG_TRACE_ERROR(x) (x)
#define D3DTRANSFORMSTATE_WORLD 1
#define D3DTRANSFORMSTATE_VIEW 2
#define D3DTRANSFORMSTATE_PROJECTION 3
#define D3DRENDERSTATE_CLIPPLANEENABLE 152
#define D3DSTATUS_CLIPUNIONLEFT 1u
#define D3DSTATUS_CLIPINTERSECTIONLEFT 0x1000u
#define D3DVIS_INSIDE_FRUSTUM 0u
#define D3DVIS_INTERSECT_FRUSTUM 1u
#define D3DVIS_OUTSIDE_FRUSTUM 2u
#define D3DVIS_INTERSECT_LEFT 4u
#define D3DVIS_OUTSIDE_LEFT 8u
#define CopyMemory(p,q,n) memcpy(p,q,n)
#define ZeroMemory(p,n) memset(p,0,n)
static D3D7Device*d3d7_device_from_iface(IDirect3DDevice7*p){return p;}
static D3D7Device*d3d3_device_from_iface(IDirect3DDevice3*p){return p;}
static unsigned checks;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"sphere visibility line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
`;
const words=data=>'{'+data.map(w=>w+'u').join(',')+'}';
const cases=String.raw`
static const DWORD matrices[][16]={MATRICES};static const DWORD centers[][3]={CENTERS};
static const struct {unsigned projection,center;DWORD radius,expected;}native[]={NATIVE};
static void reference(void){unsigned mismatches=0;D3D7Device d={0};d3d7_identity_matrix(&d.transforms[1]);d3d7_identity_matrix(&d.transforms[2]);for(unsigned i=0;i<sizeof(native)/sizeof(*native);i++){const unsigned p=native[i].projection,c=native[i].center;D3DVECTOR v;float radius;DWORD result=0xdeadbeef;memcpy(&d.transforms[3],matrices[p],64);memcpy(&v,centers[c],12);memcpy(&radius,&native[i].radius,4);MUST(!d3d3_device_ComputeSphereVisibility(&d,&v,&radius,1,0,&result));if(result!=native[i].expected){mismatches++;fprintf(stderr,"native row %u projection=%u center=%u radius=%08x actual=%08x expected=%08x\n",i,p,c,native[i].radius,result,native[i].expected);}else checks++;}MUST(!mismatches);}
static void invalid(void){D3D7Device d={0};d3d7_identity_matrix(&d.transforms[1]);d3d7_identity_matrix(&d.transforms[2]);d3d7_identity_matrix(&d.transforms[3]);D3DVECTOR center={0};float radius=1;DWORD result=0xdeadbeef;for(unsigned api=0;api<2;api++){HRESULT(*fn)(D3D7Device*,D3DVECTOR*,float*,DWORD,DWORD,DWORD*)=api?d3d7_device_ComputeSphereVisibility:d3d3_device_ComputeSphereVisibility;MUST(!fn(&d,NULL,NULL,0,0,&result)&&result==0xdeadbeef);MUST(fn(&d,NULL,&radius,1,0,&result)==DDERR_INVALIDPARAMS&&result==0xdeadbeef);MUST(fn(&d,&center,NULL,1,0,&result)==DDERR_INVALIDPARAMS&&result==0xdeadbeef);MUST(fn(&d,&center,&radius,1,0,NULL)==DDERR_INVALIDPARAMS&&result==0xdeadbeef);}memset(&d.transforms[3],0,sizeof(d.transforms[3]));MUST(d3d3_device_ComputeSphereVisibility(&d,&center,&radius,1,0,&result)==D3DERR_INVALIDMATRIX&&result==0xdeadbeef);}
int main(void){original4();original7();reference();invalid();printf("PASS actual sphere visibility C: %u checks, every frozen Device3/7 numeric case (including todo-annotated rows), 264 exact native RGB observations, separate legacy intersection margin and Device7 clip-plane flags\n",checks);return 0;}
`.replace('MATRICES',fixture.projections.map(words).join(',')).replace('CENTERS',fixture.centers.map(words).join(',')).replace('NATIVE',fixture.rows.map(r=>'{'+[r.projection,r.center,r.radius+'u',r.result+'u'].join(',')+'}').join(','));
const profiles=fs.readFileSync(root+'/runtime/d9wg_lighting_profiles.c','utf8');
const inverse=extract(profiles,'word')+'\n'+profiles.slice(profiles.indexOf('int d9wg_inverse_transpose4('),profiles.indexOf('void d9wg_lighting_legacy_geometry('));
const geometry=legacy.includes('static BOOL d3d3_sphere_planes(')?['d3d3_sphere_sqrt','d3d3_sphere_plane','d3d3_sphere_planes'].map(n=>extract(legacy,n)).join('\n'):'';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-sphere-'));
try{const source=prefix.replace('MATH_HEADER',root+'/runtime/d9wg_math.h')+['d3d7_identity_matrix','d3d7_multiply_matrix','d3d7_sqrt'].map(n=>extract(seven,n)).join('\n')+'\n'+extract(seven,'d3d7_device_ComputeSphereVisibility')+'\n'+inverse+'\n'+geometry+'\n'+extract(legacy,'d3d3_device_ComputeSphereVisibility')+'\n'+original(4)+'\n'+original(7)+'\n'+cases;fs.writeFileSync(dir+'/sphere.c',source);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/sphere.c',root+'/runtime/d9wg_math.c','-o',dir+'/sphere'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const r=spawnSync(dir+'/sphere',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
