"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../d3d9proxy/d3d9_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-record-object-mutants-"));
const cases=[
 ["recorded VB is not retained","        if (buffer) vb_binding_add_ref(buffer);","        if (buffer) (void)buffer_iface;"],
 ["recorded cube is not retained","        if (cube) cube_binding_add_ref(cube);","        if (cube) (void)cube;"],
 ["recorded VS is not retained","        if (shader) shader_binding_add_ref(shader);","        if (shader) (void)shader_iface;"],
 ["recorded PS is not retained","        if (shader) shader_binding_add_ref(shader);","        if (shader) (void)shader_iface;"],
 ["recorded declaration is not retained","        if (decl) decl_binding_add_ref(decl);","        if (decl) (void)decl_iface;"],
 ["recorded offset mutates live binding","block->streams[stream].offset = offset;","block->streams[stream].offset = offset;device->streams[stream].offset = offset;"],
 ["recorded enable creates live sparse light","D9WGLightState *current = d9wg_light_find(device->extra_lights, index);\n            entry = d9wg_light_get(&block->extra_lights, index, TRUE);","D9WGLightState *current = d9wg_light_find(device->extra_lights, index);\n            entry = d9wg_light_get(&device->extra_lights, index, TRUE);"],
 ["recorded low light appears live","block->has_light[index] = TRUE;block->lights[index] = *light;","block->has_light[index] = TRUE;block->lights[index] = *light;device->lights[index] = *light;device->light_set[index] = TRUE;"],
 ["recorded default light is empty","d9wg_light_default(&entry->light);entry->light_mask = TRUE;","memset(&entry->light,0,sizeof(entry->light));entry->light_mask = TRUE;"],
 ["invalid End cancels recording","if (!block || !block_out) return TRACE_REFUSE(D3DERR_INVALIDCALL);","if (!block || !block_out) {device->recording_result = NULL;return TRACE_REFUSE(D3DERR_INVALIDCALL);}"],
 ["End releases captured objects early","    *block_out = &block->iface;\n    return D3D_OK;\n}\n\n/* ---- IDirect3DVertexBuffer9","    state_block_release_references(block);\n    *block_out = &block->iface;\n    return D3D_OK;\n}\n\n/* ---- IDirect3DVertexBuffer9"]
];
try{for(const [label,before,after] of cases){assert(source.includes(before),label);let subject=source;
const recordedSetters={"recorded VB is not retained":"device_set_stream_source","recorded cube is not retained":"device_set_texture","recorded VS is not retained":"device_set_vertex_shader","recorded PS is not retained":"device_set_pixel_shader","recorded declaration is not retained":"device_set_vertex_declaration"};
if(recordedSetters[label]){const match=new RegExp("static HRESULT WINAPI "+recordedSetters[label]+"\\(").exec(source);assert(match,label);const at=source.indexOf("    if (device->recording_result) {",match.index);assert(at>=0,label);subject=source.slice(0,at)+source.slice(at).replace(before,after);}else subject=source.replace(before,after);
const file=path.join(directory,"objects.c");fs.writeFileSync(file,subject);const run=spawnSync(process.execPath,[path.join(__dirname,"d9wg_recording_object_test.js")],{env:{...process.env,D9WG_RECORD_OBJECT_SOURCE:file},encoding:"utf8"});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/D3D9 object recording line|Assertion.*failed|Assertion failed/,"runtime assertion required: "+label);}console.log("PASS D3D9 object recording mutations: 11/11; COM ownership, live isolation, sparse defaults, invalid End retention and completed block lifetime");}finally{fs.rmSync(directory,{recursive:true,force:true});}
