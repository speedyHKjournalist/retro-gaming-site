'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const source=fs.readFileSync(__dirname+'/../ddrawproxy/d3d_legacy_proxy.inc','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-binding-mutants-'));
const cases=[
 ['zero background clears valid binding','if (!handle || !d3d_legacy_find_material(viewport->owner, handle))','if (handle && !d3d_legacy_find_material(viewport->owner, handle))'],
 ['failed zero background overwrites state','if (!handle || !d3d_legacy_find_material(viewport->owner, handle))','if (!handle) viewport->background = 0; if (!handle || !d3d_legacy_find_material(viewport->owner, handle))'],
 ['duplicate add succeeds','if (light->viewport) return DDWG_TRACE_ERROR(D3DERR_LIGHTHASVIEWPORT);','if (light->viewport == viewport) return D3D_OK; if (light->viewport) return DDWG_TRACE_ERROR(D3DERR_LIGHTHASVIEWPORT);'],
 ['duplicate add leaks reference','if (light->viewport) return DDWG_TRACE_ERROR(D3DERR_LIGHTHASVIEWPORT);','if (light->viewport) { d3d_light_AddRef(light_iface); return DDWG_TRACE_ERROR(D3DERR_LIGHTHASVIEWPORT); }'],
 ['foreign-owner light accepted','if (light->owner != viewport->owner) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);','if (FALSE && light->owner != viewport->owner) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);'],
 ['attached light not retained','    d3d_light_AddRef(light_iface);','    (void)d3d_light_AddRef; /* missing retained light reference */'],
 ['detached light retains next edge','    light->viewport = NULL;\n    light->next_in_viewport = NULL;','    light->viewport = NULL; /* stale next edge */'],
 ['delete removes head rather than requested light','while (*link && *link != light) link = &(*link)->next_in_viewport;','while (FALSE && *link && *link != light) link = &(*link)->next_in_viewport;'],
];
try{for(const[name,before,after]of cases){assert(source.includes(before),name);const file=dir+'/mutant.inc';fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[__dirname+'/ddraw_legacy_binding_test.js'],{encoding:'utf8',env:{...process.env,DDRAW_LEGACY_BINDING_SOURCE:file}});assert.notEqual(run.status,0,'survived '+name);assert(/legacy binding line \d+:/.test(run.stderr),'compiled runtime assertion required: '+name+'\n'+run.stderr);assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(run.stderr),run.stderr);console.log('KILL '+name);}console.log('PASS actual SDK legacy binding faults: '+cases.length+'/'+cases.length);}finally{fs.rmSync(dir,{recursive:true,force:true});}
