'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-software-mutants-'));
try{for(const [name,before,after] of [
 ['float-capacity','UINT capacity = is_pixel ? D9_MAX_PS_CONST_F : device->vs_float_capacity;','UINT capacity = is_pixel ? D9_MAX_PS_CONST_F : 256;'],
 ['integer-get-capacity','!constant_range_valid(start, count, device->vs_int_capacity)','!constant_range_valid(start, count, 16)'],
 ['boolean-normalization','BOOL value = data[index] ? TRUE : FALSE;','BOOL value = data[index];'],
 ['short-state-allocation','(floats + ints) * 20u + bools * 8u','(floats + ints) * 16u + bools * 4u'],
 ['lose-program-on-reject','device->vertex_shader_hw_disabled = TRUE;','device->vertex_shader_hw_disabled = TRUE;device->vertex_shader = NULL;'],
 ['restore-failure-commits-mode','if (FAILED(hr)) return hr;\n            }\n            device->vertex_shader_hw_disabled','if (FAILED(hr)) device->software_vertex_processing = next;\n            }\n            device->vertex_shader_hw_disabled'],
]){assert.equal(source.split(before).length,2,name);const file=dir+'/'+name+'.c';fs.writeFileSync(file,source.replace(before,after));const r=spawnSync(process.execPath,[path.join(__dirname,'d9wg_software_constants_test.js')],{env:{...process.env,D9WG_SOFTWARE_SOURCE:file},encoding:'utf8'});assert.notEqual(r.status,0,name);assert(!r.stderr.includes('error:'),name+r.stderr);assert(/software constants line|AddressSanitizer/.test(r.stderr),name+r.stderr);}
console.log('PASS native software constant mutations: 6/6 capacity, normalization, allocation bounds, binding lifetime and failed mode-switch atomicity defects detected');
}finally{fs.rmSync(dir,{recursive:true,force:true});}
