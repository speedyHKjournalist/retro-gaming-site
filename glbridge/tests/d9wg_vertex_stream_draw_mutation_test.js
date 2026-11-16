"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-stream-draw-mutants-"));
const mutations=[
 ["record byte base lost","clip_draw","position = stream->base + offset*stream->stride + element->offset;","position = offset*stream->stride + element->offset;"],
 ["record attribute offset lost","clip_draw","position = stream->base + offset*stream->stride + element->offset;","position = stream->base + offset*stream->stride;"],
 ["record stream incorrectly assumed zero","clip_draw","const D9WGVertexStream *stream = s->streams + element->stream;","const D9WGVertexStream *stream = s->streams;"],
 ["colour attribute offset lost","vertex","uint32_t at = first[element->stream] + vertex*stream->stride + element->offset;","uint32_t at = first[element->stream] + vertex*stream->stride + (element->usage == 10 ? 0u : element->offset);"],
 ["virtual source drops signed base","clip_draw_header","(int64_t)s->first_vertex + s->base_vertex + index;","(int64_t)s->first_vertex + (s->stream_layout ? 0 : s->base_vertex) + index;"],
 ["virtual source repeats its first vertex","vertex","*offset = (uint32_t)vertex;return 1;","*offset = 0;return 1;"],
];
try{
 if(process.argv.includes("--previous-source")){
  const previous=spawnSync("git",["show","8aa5a10:glbridge/runtime/d9wg_clip_draw.c"],{cwd:path.resolve(root,".."),encoding:"utf8"});assert.equal(previous.status,0,previous.stderr);
  const file=path.join(directory,"previous.c");fs.writeFileSync(file,previous.stdout);
  const result=spawnSync(process.execPath,[path.join(__dirname,"d9wg_vertex_stream_draw_test.js")],{encoding:"utf8",env:{...process.env,D9WG_CLIP_DRAW_SOURCE:file}});
  assert.notEqual(result.status,0,"previous source accepted virtual streams unexpectedly");assert.match(result.stderr,/multi-stream draw mismatch line/);
  console.log("PASS previous production C source 8aa5a10 fails the new multi-stream draw check");
 }
 for(const [label,name,before,after] of mutations){const header=name==="clip_draw_header",original=fs.readFileSync(path.join(root,"runtime/d9wg_"+(header?"clip_draw.h":name+".c")),"utf8");assert.equal(original.split(before).length,2,"unique mutation: "+label);
  const caseDirectory=fs.mkdtempSync(path.join(directory,"case-")),file=path.join(caseDirectory,(header?"clip_draw":name)+".c");
  if(header){fs.writeFileSync(path.join(caseDirectory,"d9wg_clip_draw.h"),original.replace(before,after));fs.copyFileSync(path.join(root,"runtime/d9wg_clip_draw.c"),file);}else fs.writeFileSync(file,original.replace(before,after));
  const result=spawnSync(process.execPath,[path.join(__dirname,"d9wg_vertex_stream_draw_test.js")],{encoding:"utf8",env:{...process.env,["D9WG_"+(header?"CLIP_DRAW":name.toUpperCase())+"_SOURCE"]:file}});assert.notEqual(result.status,0,"survived: "+label);assert.match(result.stderr,/multi-stream draw mismatch line/,"must execute an assertion: "+label);
 }
 console.log("PASS multi-stream clipping semantic mutations: 6/6; production sources untouched");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
