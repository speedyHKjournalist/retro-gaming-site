"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-light-query-mutants-"));
const cases=[
 ['runtime/d9wg_light_state.h','D9WG_LIGHT_QUERY_HEADER','enabled returns one','enabled ? 0x80 : 0','enabled ? 1 : 0'],
 ['runtime/d9wg_light_state.h','D9WG_LIGHT_QUERY_HEADER','unknown succeeds','if (!known || !output) return 0;','if (!output) return 0;(void)known;'],
 ['runtime/d9wg_light_state.h','D9WG_LIGHT_QUERY_HEADER','failure clears output','if (!known || !output) return 0;','if (!known || !output) {if (output) *output = 0;return 0;}'],
 ['d3d8proxy/d3d8_proxy.c','D8WG_LIGHT_QUERY_SOURCE','disabled known light considered missing','known = device->light_set[index];enabled = device->light_enabled[index];','known = device->light_enabled[index];enabled = device->light_enabled[index];'],
 ['d3d9proxy/d3d9_proxy.c','D9WG_LIGHT_QUERY_SOURCE','allocated sparse node considered initialized','known = entry && entry->set;enabled = entry ? entry->enabled : FALSE;','known = entry != NULL;enabled = entry ? entry->enabled : FALSE;'],
 ['ddrawproxy/d3d7_proxy.inc','D7WG_LIGHT_QUERY_SOURCE','D3D7 bypasses canonical result','return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);\n    return D3D_OK;\n}\n\nstatic HRESULT WINAPI d3d7_device_SetClipPlane','return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);\n    *enable = enabled;return D3D_OK;\n}\n\nstatic HRESULT WINAPI d3d7_device_SetClipPlane']
];
try{for(const [relative,variable,label,before,after] of cases){const source=fs.readFileSync(path.join(root,relative),'utf8');assert.equal(source.split(before).length,2,label);const file=path.join(directory,path.basename(relative));fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,'d9wg_light_enable_query_test.js')],{env:{...process.env,[variable]:file},encoding:'utf8'});assert.notEqual(run.status,0,'survived '+label);assert.match(run.stderr,/light query API [789] line/,'runtime assertion required: '+label);}console.log('PASS light-enable query mutations: 6/6; exact native BOOL, unknown/null handling, output preservation and all three actual frontends');}finally{fs.rmSync(directory,{recursive:true,force:true});}
