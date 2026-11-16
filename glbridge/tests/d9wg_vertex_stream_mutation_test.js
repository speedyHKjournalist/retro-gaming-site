"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-stream-mutants-"));
const mutations=[
 ["zero stride applies the source index","*first = base;return 1;","*first = base + start;return 1;"],
 ["stream offset ignored","+ vertex*stream->stride + element->offset;","+ vertex*stream->stride;"],
 ["constant attribute advances","vertex*stream->stride + element->offset;","vertex*(stream->stride ? stream->stride : 1u) + element->offset;"],
 ["last vertex range unchecked","layout->extent[stream], start, count, first + stream)","layout->extent[stream], start, count ? 1u : 0u, first + stream)"],
 ["source index discarded","d9wg_process_stream_validate(layout, streams, source_start, count, first)","d9wg_process_stream_validate(layout, streams, source_start ? 0u : 0u, count, first)"],
 ["clip records repeatedly overwrite first","state.clip_vertices = vertex_state->clip_vertices + i;","state.clip_vertices = vertex_state->clip_vertices;"],
 ["late palette validation after earlier writes","uint32_t indices[4];gather_stream_vertex(layout, streams, first, i, vertex);","if (i + 1u == count) break;uint32_t indices[4];gather_stream_vertex(layout, streams, first, i, vertex);"],
];
try{
 const original=fs.readFileSync(path.join(root,"runtime/d9wg_vertex.c"),"utf8");
 for(const [label,before,after] of mutations){assert.equal(original.split(before).length,2,"unique mutation: "+label);const file=path.join(directory,"vertex.c");fs.writeFileSync(file,original.replace(before,after));
  const result=spawnSync(process.execPath,[path.join(__dirname,"d9wg_vertex_stream_test.js")],{encoding:"utf8",env:{...process.env,D9WG_VERTEX_SOURCE:file}});
  assert.notEqual(result.status,0,"survived: "+label);assert.match(result.stderr,/stream mismatch line/,"must execute an assertion: "+label);
 }
 console.log("PASS multi-stream semantic mutations: 7/7; production sources untouched");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
