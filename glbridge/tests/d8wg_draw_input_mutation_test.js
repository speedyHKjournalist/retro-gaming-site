"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../d3d8proxy/d3d8_proxy.c"),"utf8"),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-draw-mutants-"));
const cases=[
 ["secondary extent ignored","d8_draw_stream_range","if (extent[stream] < end)","if (stream == 15 && extent[stream] < end)"],
 ["foreign draw buffers accepted","d8_draw_stream_range","buffer->device != device ||","0 ||"],
 ["locked draw buffers accepted","d8_draw_stream_range","|| buffer->locked ||","|| 0 ||"],
 ["draw starts at zero","d8_draw_stream_range","extent[stream], start, count, &first)","extent[stream], (start & 0u), count, &first)"],
 ["draw ignores final vertex","d8_draw_stream_range","extent[stream], start, count, &first)","extent[stream], start, 1u + (count & 0u), &first)"],
 ["UP allows multiple streams","d8_draw_stream_range","(up_stride && stream)","(0 && up_stride && stream)"],
 ["UP ignores element extent","d8_draw_stream_range","if (extent[stream] > up_stride)","if (0 && extent[stream] > up_stride)"],
 ["declaration clips unavailable","d8_prepare_clip_status","if (!d8_current_source_elements(","if ((device->vertex_shader & 1u) || !d8_current_source_elements("],
 ["clips ignore base vertex","d8_prepare_clip_status","source = *input;","source = *input;source.base_vertex = 0;"],
 ["clips lose user planes","d8_prepare_clip_status","device->clip_planes, next)","(const float[6][4]){{0}}, next)"],
 ["failed transport updates ClipStatus","device_draw_primitive","if (emitted) { device->clip_status","if (1) { device->clip_status"],
];
try{for(const [label,name,before,after] of cases){const match=new RegExp("static (?:BOOL|HRESULT)(?: WINAPI)? "+name+"\\(").exec(source);assert(match,name);const start=match.index,end=source.indexOf("\nstatic ",start+1),part=source.slice(start,end);assert.equal(part.split(before).length,2,label);const file=path.join(directory,"draw.c");fs.writeFileSync(file,source.slice(0,start)+part.replace(before,after)+source.slice(end));const run=spawnSync(process.execPath,[path.join(__dirname,"d8wg_draw_input_test.js")],{env:{...process.env,D8WG_DRAW_INPUT_SOURCE:file},encoding:"utf8"});assert.notEqual(run.status,0,"survived "+label);assert.match(run.stderr,/D3D8 draw input line/,"runtime assertion required: "+label);}console.log("PASS D3D8 draw-input mutations: 11/11; stream extents/ownership/locks/ranges, UP restrictions, declaration/base/user-plane clip semantics and atomic transport failure");}finally{fs.rmSync(directory,{recursive:true,force:true});}
