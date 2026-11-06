"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-mapped-mutants-"));
const cases=[
 ["late invalid range emits a prefix","policy","d9wg_flush_mapped_buffers","map->offset > map->length ||","0 ||"],
 ["duplicate metadata ignored","policy","d9wg_flush_mapped_buffers","if (prior->identity == map->identity &&","if (0 && prior->identity == map->identity &&"],
 ["readonly map uploaded","policy","d9wg_flush_mapped_buffers","if (map->flags & D9WG_BUFFER_LOCK_READONLY)","if (0 && (map->flags & D9WG_BUFFER_LOCK_READONLY))"],
 ["duplicate map uploaded twice","policy","d9wg_flush_mapped_buffers","if (j < i) continue;","if (0 && j < i) continue;"],
 ["only first map flushed","policy","d9wg_flush_mapped_buffers","i < count; ++i) {\n        D9WGMappedBuffer *map","i < (count ? 1u : 0u); ++i) {\n        D9WGMappedBuffer *map"],
 ["shadow offset omitted","policy","d9wg_flush_mapped_buffers","map->shadow + map->offset","map->shadow + (map->offset & 0u)"],
 ["upload range truncated","policy","d9wg_flush_mapped_buffers","map->size, map->flags)","(map->size ? map->size - 1u : 0u), map->flags)"],
 ["streaming flags lost","policy","d9wg_flush_mapped_buffers","map->size, map->flags)","map->size, map->flags & ~D9WG_BUFFER_LOCK_DISCARD)"],
 ["failed callback gets a receipt","policy","d9wg_flush_mapped_buffers","        if (!emit(context,","        map->uploaded = 1;\n        if (!emit(context,"],
 ["accepted callback loses its receipt","policy","d9wg_flush_mapped_buffers","map->uploaded = 1;","map->uploaded = 0;"],
 ["zero-size maps skipped","policy","d9wg_flush_mapped_buffers","        if (!emit(context,","        if (!map->size) continue;\n        if (!emit(context,"],
 ["foreign mapped buffer accepted", "bridge", "d9wg_flush_draw_buffers", "if ((buffer)->device != device) return FALSE; \\\n        maps[count]", "if (0 && (buffer)->device != device) return FALSE; \\\n        maps[count]"],
 ["last stream missing","bridge","d9wg_flush_draw_buffers","i < sizeof(device->streams)/sizeof(device->streams[0]);","i + 1u < sizeof(device->streams)/sizeof(device->streams[0]);"],
 ["index map not collected","bridge","d9wg_flush_draw_buffers","if (indexed) {","if (0 && indexed) {"],
 ["accepted prefix not committed","bridge","d9wg_flush_draw_buffers","    for (uint32_t i = 0; i < count; ++i) {\n        if (!maps[i].uploaded)","    if (!result) return FALSE;\n    for (uint32_t i = 0; i < count; ++i) {\n        if (!maps[i].uploaded)"],
 ...["VERTEX","INDEX"].flatMap(kind=>[
  [kind+" re-discards after draw","bridge","d9wg_flush_draw_buffers","D9WG_MAPPED_"+kind+" *buffer = objects[i];\n            buffer->lock_flags &= ~D9WG_BUFFER_LOCK_DISCARD;","D9WG_MAPPED_"+kind+" *buffer = objects[i];\n            buffer->lock_flags &= UINT32_MAX;"],
  [kind+" unlocked by draw","bridge","d9wg_flush_draw_buffers","D9WG_MAPPED_"+kind+" *buffer = objects[i];","D9WG_MAPPED_"+kind+" *buffer = objects[i];buffer->locked = FALSE;"],
 ]),
 ...[8,9].flatMap(api=>[
  ["normal draw omits flush API "+api,api,"device_draw_primitive","if (!d9wg_flush_draw_buffers(device, FALSE))","if (0 && !d9wg_flush_draw_buffers(device, FALSE))"],
  ["indexed draw omits index flush API "+api,api,"device_draw_indexed"+(api===9?"_primitive":""),"d9wg_flush_draw_buffers(device, TRUE)","d9wg_flush_draw_buffers(device, FALSE)"],
  ["draw CPU input rejects mapped sources API "+api,api,"d"+api+"_prepare_"+(api===9?"stream_":"")+"clip_status",api===8?"input->vertex_count - first, TRUE, &streams)":"(UINT)start, count, TRUE, &input)",api===8?"input->vertex_count - first, FALSE, &streams)":"(UINT)start, count, FALSE, &input)"],
 ])
];
function span(source,name){const m=new RegExp("(?:static )?(?:int|BOOL|HRESULT)(?: WINAPI)? "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return[m.index,at];}
try{for(const[label,kind,name,before,after]of cases){const file=kind==="policy"?root+"/runtime/d9wg_buffer_lock.c":kind==="bridge"?root+"/runtime/d9wg_mapped_draw_bridge.h":root+"/d3d"+kind+"proxy/d3d"+kind+"_proxy.c",source=readComparisonSource(file,"utf8"),[start,end]=span(source,name),part=source.slice(start,end);assert.equal(part.split(before).length,2,label);const mutated=dir+(kind==="bridge"?"/bridge.h":"/source.c");fs.writeFileSync(mutated,source.slice(0,start)+part.replace(before,after)+source.slice(end));const shared=typeof kind==="string",test=shared?"d9wg_mapped_draw_test.js":kind===8?"d8wg_draw_input_test.js":"d9wg_bound_draw_test.js",key=kind==="policy"?"D9WG_MAPPED_POLICY_SOURCE":kind==="bridge"?"D9WG_MAPPED_BRIDGE_HEADER":kind===8?"D8WG_DRAW_INPUT_SOURCE":"D9WG_BOUND_DRAW_SOURCE",r=spawnSync(process.execPath,[path.join(__dirname,test)],{encoding:"utf8",env:{...process.env,[key]:mutated}});assert.notEqual(r.status,0,"survived "+label);assert.match(r.stderr,/mapped draw line|D3D[89] (?:bound draw|draw input) line/,"runtime assertion required "+label);}
 console.log("PASS mapped draw mutations: "+cases.length+"/"+cases.length+"; exact uploads, readonly/dedup, partial receipts, retained locks, VB/IB DISCARD and actual draw/CPU-input ordering");
}finally{fs.rmSync(dir,{recursive:true,force:true});}
