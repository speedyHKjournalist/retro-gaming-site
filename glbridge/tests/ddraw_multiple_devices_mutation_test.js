"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/d3d7_proxy.inc"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-multiple-mutants-"));
const cases=[
 ["older surface view rejected","(const void *)surface_iface->lpVtbl != &g_surface_vtbl1 &&","TRUE &&"],
 ["foreign vtable accepted","(const void *)surface_iface->lpVtbl != &g_surface_vtbl4))","FALSE))"],

 ["legacy lighting profile omitted","create.behavior_flags = D9WG_DEVICE_LEGACY_LIGHTING;","create.behavior_flags = 0;"],
 ["legacy lighting profile leaks to Device7","if (device->creation_version < 7u)\n        create.behavior_flags = D9WG_DEVICE_LEGACY_LIGHTING;","if (device->creation_version <= 7u)\n        create.behavior_flags = D9WG_DEVICE_LEGACY_LIGHTING;"],
 ["invalid target reaches transport","if (FAILED(hr)) return DDWG_TRACE_ERROR(hr);","(void)hr;"],
 ["depth attachment updates only one context","if (device->render_target == surface) d3d7_emit_render_target(device);","if (device == object->d3d7_device && device->render_target == surface) d3d7_emit_render_target(device);"],
 ["flip updates only one context","for (device = object ? object->d3d7_device : NULL; device; device = device->next_in_owner) {\n        d3d7_emit_render_target(device);","for (device = object ? object->d3d7_device : NULL; device; device = device->next_in_owner) {\n        if (device != object->d3d7_device) continue; d3d7_emit_render_target(device);"],
 ["second device rejected","if (!ensure_device(owner)) return DDWG_TRACE_ERROR(D3DERR_INITFAILED);","if (owner->d3d7_device || !ensure_device(owner)) return DDWG_TRACE_ERROR(D3DERR_INITFAILED);"],
 ["contexts share owner handle","device->wire_handle = allocate_handle();","device->wire_handle = (allocate_handle(), device->owner->device_handle);"],
 ["context creates a visible surface","hidden.flags = D9WG_WINDOW_NO_SURFACE;","hidden.flags = 0;"],
 ["failed context erases siblings","!d3d7_sync_initial_state(device)) {","!d3d7_sync_initial_state(device)) { owner->d3d7_device = NULL;"],
 ["middle release erases siblings","if (*link) *link = device->next_in_owner;","owner->d3d7_device = NULL;"],
 ["host context leaked","if (device->wire_created) {","if (FALSE && device->wire_created) {"],
];
try{for(const[label,before,after]of cases){assert.equal(source.split(before).length,2,label);const file=dir+"/proxy.inc";fs.writeFileSync(file,source.replace(before,after));const run=spawnSync(process.execPath,[path.join(__dirname,"ddraw_multiple_devices_test.js")],{encoding:"utf8",env:{...process.env,DDRAW_MULTIPLE_DEVICE_SOURCE:file}});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/multiple device line \d+:/,"strict compilation and runtime assertion required: "+label+"\n"+run.stderr);assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(run.stderr),run.stderr);console.log("KILL "+label);}console.log("PASS multiple device mutations: "+cases.length+"/"+cases.length+" compiled implementations rejected by context ownership assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
