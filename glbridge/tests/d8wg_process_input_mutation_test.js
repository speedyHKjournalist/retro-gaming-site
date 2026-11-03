"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../d3d8proxy/d3d8_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-input-mutants-"));
const start=source.indexOf("static HRESULT d8_process_input("),end=source.indexOf("static int d8_clip_emit(",start);assert(start>=0&&end>start);const helper=source.slice(start,end);
const cases=[
 ["programmable shader accepted as fixed input","all","!device->vertex_shader_ref->declaration_only)","0)"],
 ["stream ranges unchecked","helper","if (!d9wg_process_stream_validate(","if (0 && !d9wg_process_stream_validate("],
 ["foreign buffers accepted","helper","buffer->device != device ||","0 ||"],
 ["locked buffers accepted","helper","(buffer->locked && !allow_locked)","(buffer->locked && allow_locked)"],
 ["constant stream sent to strided fast path","helper","input->layout.stream_mask != 1u || !input->streams[0].stride","input->layout.stream_mask != 1u"],
 ["snapshot does not preserve input bytes","helper","CopyMemory(input->snapshot, destination->shadow, destination->length);","memset(input->snapshot, 0, destination->length);"],
 ["aliased stream reads live output","helper","input->streams[i].vertices = input->snapshot;","input->streams[i].vertices = destination->shadow;"],
 ["aliased streams allocate separate snapshots","helper","if (!input->snapshot) {\n            input->snapshot = HeapAlloc","if (1) {\n            input->snapshot = HeapAlloc"],
];
try{for(const [label,scope,before,after] of cases){const original=scope==="helper"?helper:source;assert.equal(original.split(before).length,2,label);const changed=original.replace(before,after),file=path.join(directory,"input.c");fs.writeFileSync(file,scope==="helper"?source.slice(0,start)+changed+source.slice(end):changed);const run=spawnSync(process.execPath,[path.join(__dirname,"d8wg_process_input_test.js")],{env:{...process.env,D8WG_PROCESS_INPUT_SOURCE:file},encoding:"utf8"});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/D3D8 process input line/,"runtime assertion required: "+label);}console.log("PASS D3D8 process-input mutations: 8/8; shader mode, ranges, ownership, locks, constant input and shared pre-call alias snapshot");}finally{fs.rmSync(directory,{recursive:true,force:true});}
