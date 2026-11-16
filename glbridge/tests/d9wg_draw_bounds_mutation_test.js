"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-draw-mutants-"));
const cases=[
 ["NULL nonindexed stream refused",9,"d9_bound_draw_range","if (!buffer) continue;","if (!buffer) return D3DERR_INVALIDCALL;"],
 ["NULL indexed stream refused",9,"d9_draw_index_bounds","if (!buffer) continue;","if (!buffer) return D3DERR_INVALIDCALL;"],
 ["constant indexed draw discarded",9,"device_draw_indexed_primitive","if (!bound) return device_draw_primitive(iface, primitive_type, 0, primitive_count);","if (!bound) return D3D_OK;"],
 ["extent lost","vertex","d9wg_draw_index_bounds","if (extent[stream] < end)","if (stream==15 && extent[stream] < end)"],
 ["negative lower bound lost","vertex","d9wg_draw_index_bounds","-(int64_t)base_vertex : 0","0 * (int64_t)base_vertex : 0"],
 ["base omitted from upper bound","vertex","d9wg_draw_index_bounds","end - base_vertex + 1","end - (base_vertex & 0) + 1"],
 ["last vertex excluded","vertex","d9wg_draw_index_bounds","end - base_vertex + 1","end - base_vertex"],
 ["offset omitted","vertex","d9wg_draw_index_bounds","stream->length - stream->base - extent[i]","stream->length - extent[i]"],
 ["instance range ignored","vertex","d9wg_draw_index_bounds","vertices = (instances - 1u)/divisor + 1u","vertices = 1u + ((instances - 1u)/divisor & 0u)"],
 ["constant stream constrained","vertex","d9wg_draw_index_bounds","if (stream->stride && !(frequency","if ((stream->stride || 1) && !(frequency"],
 ["failure publishes bounds","vertex","d9wg_draw_index_bounds","uint32_t extent[D9WG_VERTEX_MAX_STREAMS] = {0};","uint32_t extent[D9WG_VERTEX_MAX_STREAMS] = {0};result->min_vertex=0;"],
 ["hint outside binding accepted","vertex","d9wg_draw_status_hints","first < bounds->min_vertex ||","0 ||"],
 ["hint beyond binding accepted","vertex","d9wg_draw_status_hints","count > bounds->vertex_limit - first","0 * count > bounds->vertex_limit - first"],
 ["index below hint accepted","vertex","d9wg_draw_status_hints","index < first || index - first >= count","index >= first && index - first >= count"],
 ["index past hint accepted","vertex","d9wg_draw_status_hints","index < first || index - first >= count","index < first"],
 ["empty programmable declaration refused",9,"d9_draw_index_bounds","bounds->min_vertex = 0;bounds->vertex_limit = UINT32_MAX;return D3D_OK;","bounds->min_vertex = 0;bounds->vertex_limit = UINT32_MAX;return D3DERR_INVALIDCALL;"],
 ["valid multi-stream hints cannot recover status",9,"d9_prepare_stream_clip_status","if (!result) *valid = TRUE;","if (!result) *valid = FALSE;"],
 ["valid hints cannot recover status",9,"d9_prepare_clip_status","if (!error && (!layout.position_transformed || source.status_screen)) *valid = TRUE;","if (!error && (!layout.position_transformed || source.status_screen)) *valid = FALSE;"],
 ...[8,9].flatMap(api=>[
  ["transformed draw scans indices API "+api,api,"device_draw_indexed"+(api===9?"_primitive":""),"status_source.indices, status_index_count, index_size)","status_source.indices, (status_index_count ? status_index_count : index_count), index_size)"],
  ["zero draws rejected API "+api,api,"device_draw_primitive","if (!primitive_count) return D3D_OK;","if (!primitive_count) return D3DERR_INVALIDCALL;"],
  ["zero indexed draws rejected API "+api,api,"device_draw_indexed"+(api===9?"_primitive":""),"if (!primitive_count) return D3D_OK;","if (!primitive_count) return D3DERR_INVALIDCALL;"],
  ["zero vertex hint rejected API "+api,api,"device_draw_indexed"+(api===9?"_primitive":""),"    index_size = device->index_buffer->format","    if (!vertex_count) return D3DERR_INVALIDCALL;\n    index_size = device->index_buffer->format"],
  ["unknown status remains valid API "+api,api,"d"+api+"_prepare_clip_status","if (input->indices && input->min_vertex >= input->vertex_count)","if (0 && input->indices && input->min_vertex >= input->vertex_count)"],
 ])
];
function function_span(source,name){const m=new RegExp("(?:static )?(?:int|BOOL|HRESULT)(?: WINAPI)? "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return[m.index,at];}
try{for(const[label,kind,name,before,after]of cases){const file=kind==="vertex"?root+"/runtime/d9wg_vertex.c":root+"/d3d"+kind+"proxy/d3d"+kind+"_proxy.c",source=readComparisonSource(file,"utf8"),[start,end]=function_span(source,name),part=source.slice(start,end);assert.equal(part.split(before).length,2,label);const mutated=dir+"/source.c";fs.writeFileSync(mutated,source.slice(0,start)+part.replace(before,after)+source.slice(end));const test=kind==="vertex"?"d9wg_draw_index_bounds_test.js":kind===8?"d8wg_draw_input_test.js":"d9wg_bound_draw_test.js",key=kind==="vertex"?"D9WG_VERTEX_SOURCE":kind===8?"D8WG_DRAW_INPUT_SOURCE":"D9WG_BOUND_DRAW_SOURCE",r=spawnSync(process.execPath,[path.join(__dirname,test)],{encoding:"utf8",env:{...process.env,[key]:mutated}});assert.notEqual(r.status,0,"survived "+label);assert.match(r.stderr,/index bounds line|D3D[89] (?:bound draw|draw input) line|runtime error: division by zero|AddressSanitizer|native run signal SIG(?:SEGV|BUS)/,"runtime failure required "+label);}
 console.log("PASS indexed draw bounds mutations: "+cases.length+"/"+cases.length+"; shared ranges/status hints and actual API 8/9 zero/optional-hint/status behavior");
}finally{fs.rmSync(dir,{recursive:true,force:true});}
