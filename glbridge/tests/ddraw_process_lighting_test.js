'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.DDRAW_PROCESS_LIGHT_SOURCE||root+'/ddrawproxy/d3d7_proxy.inc','utf8');
const {testPath}=require('./host_paths.js');
const native=require(testPath('fixtures/ddraw_native_lighting.json'));
function extract(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return source.slice(m.index,end).replace(/\bWINAPI\b/g,'');}
const methods=['d3d7_multiply_matrix','d3d7_device_SetMaterial','d3d7_device_GetMaterial','d3d7_prepare_lighting','d3d7_upload_vertex_range','d3d7_process_vertex_bytes'].map(extract).join('\n');
const rows='static const struct Row{unsigned api;const char*phase;unsigned kind,power,colors,cv,source,vertex;DWORD diffuse,specular;}rows[]={'+native.rows.map(r=>'{'+r.api+',"'+r.phase+'",'+[r.kind,r.power,r.colors,r.cv,r.source,r.vertex,r.diffuse,r.specular].map(n=>n+'u').join(',')+'}').join(',')+'};\n';
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-process-lighting-'));
try{
 const sdk=dir+'/sdk.c';fs.writeFileSync(sdk,'#define COBJMACROS\n#include <d3d.h>\n#include "'+root+'/ddrawproxy/ddraw_protocol.h"\n#define DDWG_TRACE_ERROR(h) (h)\n#define D3D7_MAX_LIGHTS 8u\nstatic int lighting_begin;\n'+methods+'\n'+rows+fs.readFileSync(__dirname+'/native/ddraw_process_lighting_cases.h','utf8')+'\nstatic int lighting_end;');
 const pp=spawnSync(process.env.D9WG_CC||'i686-w64-mingw32-gcc',['-E','-P',sdk],{encoding:'utf8',maxBuffer:16<<20});assert.equal(pp.status,0,pp.stderr);const body=pp.stdout.split('static int lighting_begin;')[1].split('static int lighting_end;')[0];
 const enums=['D3DTRANSFORMSTATETYPE','D3DRENDERSTATETYPE'].map(name=>{const m=new RegExp('typedef enum [^{}]*\\{[^{}]*\\} '+name+';').exec(pp.stdout);assert(m,name);return m[0];}).join('\n');
 fs.writeFileSync(dir+'/test.c',fs.readFileSync(__dirname+'/native/ddraw_process_lighting_prefix.h','utf8')+enums+body);
 const sources=['clip','blend','vertex','fvf','math','lighting','tnl_math'].map(n=>n==='lighting'&&process.env.DDRAW_PROCESS_LIGHT_KERNEL||root+'/runtime/d9wg_'+n+'.c');
 const built=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-O1','-ffp-contract=off','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',root,'-I',root+'/runtime',dir+'/test.c',...sources,'-o',dir+'/test'],{encoding:'utf8'});assert.equal(built.status,0,built.stderr);
 const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
