"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/d3d_legacy_proxy.inc"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-handles-mutants-"));
const cases=[
 ["shared material misses foreign owner","if (device->legacy_material == material) {","if (device->owner == material->owner && device->legacy_material == material) {"],
 ["matrix lookup limited to caller","D3DLegacyMatrix **link = d3d_legacy_matrix_link(handle);\n    (void)device;","D3DLegacyMatrix **link = &device->legacy_matrices; while (*link && (*link)->handle != handle) link = &(*link)->next; if (!*link) link = NULL;"],
 ["material aliases merged","if (entry->device == device) { *handle = entry->handle; return D3D_OK; }","if (entry->device || device) { *handle = entry->handle; return D3D_OK; }"],
 ["foreign texture handle rejected","if (!handle || !device || format_is_depth(surface->format))","if (!handle || !device || device->owner != surface->owner || format_is_depth(surface->format))"],
 ["uninitialized handles swapped","if (!first_handle || !second_handle) return DDWG_TRACE_ERROR(E_INVALIDARG);","if (0 && (!first_handle || !second_handle)) return DDWG_TRACE_ERROR(E_INVALIDARG);"],
 ["dead device aliases retained","if (entry->device != device) { link = &entry->next; continue; }","if (entry->device || device) { link = &entry->next; continue; }"],
 ["foreign bound context not refreshed","DWORD current = device->render_states[D3DRENDERSTATE_TEXTUREHANDLE];","DWORD current = device->render_states[D3DRENDERSTATE_TEXTUREHANDLE]; if (device->owner != first->owner) continue;"],
 ["swap changes stored handle value","D3DRENDERSTATE_TEXTUREHANDLE, current);","D3DRENDERSTATE_TEXTUREHANDLE, first_handle);"],
];
try{for(const[label,before,after]of cases){assert.equal(source.split(before).length,2,label);const file=dir+"/proxy.inc";fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,"ddraw_legacy_handles_test.js")],{encoding:"utf8",env:{...process.env,DDRAW_LEGACY_HANDLES_SOURCE:file}});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/legacy handles line \d+:/,"strict compilation and runtime assertion required: "+label+"\n"+run.stderr);assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(run.stderr),run.stderr);console.log("KILL "+label);}console.log("PASS legacy handle mutations: 8/8 compiled implementations rejected by native identity/lifetime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
