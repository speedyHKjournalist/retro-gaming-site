"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-stream-clip-mutants-"));
const mutations=[
 ["signed base discarded","+ draw->base_vertex + begin;","+ (draw->base_vertex ? 0 : 0) + begin;"],
 ["raw index end accepted","index >= end","index > end"],
 ["raw index below minimum accepted","index < begin || ",""],
 ["unreferenced declared vertices omitted","i < end - begin;","i < (draw->indices ? 1u : end - begin);"],
 ["partial status published","d9wg_vertex_clip_codes(&source, world_view_projection, state, &result)","d9wg_vertex_clip_codes(&source, world_view_projection, state, status)"],
 ["last source range not validated","(uint32_t)start, end - begin, first)","(uint32_t)start, 1, first)"],
 ["world user planes ignored","uint8_t vertex[D9WG_VERTEX_GATHER_BYTES];","uint8_t vertex[D9WG_VERTEX_GATHER_BYTES];D9WGVertexState no_planes=*state;no_planes.clip_plane_mask=0;state=&no_planes;"],
 ["immediate POSITIONT never classified","D9WGClipDrawSource source = *draw;","D9WGClipDrawSource source = *draw;source.status_screen = 0;"],
];
try{
 const original=fs.readFileSync(path.join(root,"runtime/d9wg_vertex.c"),"utf8"),at=original.indexOf("int d9wg_vertex_stream_clip_codes("),prefix=original.slice(0,at),source=original.slice(at);assert(at>0);
 for(const [label,before,after] of mutations){assert.equal(source.split(before).length,2,"unique mutation: "+label);const file=path.join(directory,"vertex.c");fs.writeFileSync(file,prefix+source.replace(before,after));
  const result=spawnSync(process.execPath,[path.join(__dirname,"d9wg_vertex_stream_clip_test.js")],{encoding:"utf8",env:{...process.env,D9WG_VERTEX_SOURCE:file}});assert.notEqual(result.status,0,"survived: "+label);assert.match(result.stderr,/multi-stream clip mismatch line/,"must execute an assertion: "+label);
 }
 console.log("PASS multi-stream clip semantic mutations: 8/8; production sources untouched");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
