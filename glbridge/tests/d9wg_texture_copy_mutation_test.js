"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-copy-mutants-"));
const cases=[
 ["source skip omitted","plan","d9wg_texture_copy_plan","while (size > target)","while (0 && size > target)"],
 ["source availability not reduced","plan","d9wg_texture_copy_plan","source->levels - skip : 0","source->levels : 0"],
 ["missing source mip underflows","plan","d9wg_texture_copy_plan","skip < source->levels ? source->levels - skip : 0","source->levels - skip"],
 ["destination count ignored","plan","d9wg_texture_copy_plan","if (levels > destination->levels)","if (0 && levels > destination->levels)"],
 ["depth ignored for mip selection","plan","texture_copy_max_dimension","value > extent->depth ? value : extent->depth","value + (extent->depth & 0u)"],
 ["axis clamping lost","plan","d9wg_texture_copy_plan","if (!width) width = 1;","if (!width) width = 0;"],
 ["callback source level not shifted","plan","d9wg_texture_copy_levels","plan->source_first + level, level)","(plan->source_first & 0u) + level, level)"],
 ["copy runs before prepare","plan","d9wg_texture_copy_levels","pass == 1 ? prepare : copy","pass == 1 ? copy : prepare"],
 ["callback errors swallowed","plan","d9wg_texture_copy_levels","if (result) return result;","if (0 && result) return result;"],
 ["source lock ignored","bridge","d9wg_validate_texture_copy_level","from->locked ||","0 ||"],
 ["late destination lock ignored","bridge","d9wg_validate_texture_copy_level","to->locked ||","0 ||"],
 ["byte size mismatch ignored","bridge","d9wg_validate_texture_copy_level","from->byte_count != to->byte_count ||","0 ||"],
 ["row pitch mismatch ignored","bridge","d9wg_validate_texture_copy_level","from->row_pitch != to->row_pitch ||","0 ||"],
 ["per-level width ignored","bridge","d9wg_validate_texture_copy_level","from->width != to->width ||","0 ||"],
 ["cube source stride uses destination count","bridge","d9wg_validate_texture_copy_level","layer*copy->source_stride + source_level","layer*copy->destination_stride + source_level"],
 ["cube destination stride uses source count","bridge","d9wg_validate_texture_copy_level","layer*copy->destination_stride + destination_level","layer*copy->source_stride + destination_level"],
 ["existing shadows replaced","bridge","d9wg_prepare_texture_copy_level","if (!to->shadow) {","if (1) {"],
 ["source pixels overwritten","bridge","d9wg_commit_texture_copy_level","CopyMemory(to->shadow, from->shadow, to->byte_count)","CopyMemory((void*)from->shadow, to->shadow, to->byte_count)"],
 ["last copied byte omitted","bridge","d9wg_commit_texture_copy_level","from->shadow, to->byte_count)","from->shadow, to->byte_count - 1u)"],
 ["volume routed as 2D","bridge","d9wg_commit_texture_copy_level","if (copy->kind == D9WG_COPY_VOLUME)","if (0 && copy->kind == D9WG_COPY_VOLUME)"],
 ["cube face always zero","bridge","d9wg_commit_texture_copy_level","copy->texture, layer, destination_level, &full)","copy->texture, layer & 0u, destination_level, &full)"],
 ["allocation failure misclassified","bridge","d9wg_update_texture_common","result == D9WG_TEXTURE_COPY_OUT_OF_MEMORY ? E_OUTOFMEMORY","result == D9WG_TEXTURE_COPY_OUT_OF_MEMORY ? D3DERR_INVALIDCALL"],
 ["upload failure reported as success","bridge","d9wg_update_texture_common","result ? D3DERR_DRIVERINTERNALERROR : D3D_OK","result ? D3D_OK : D3D_OK"],
 ...[8,9].map(api=>["larger 2D source rejected API "+api,api,"device_update_texture",api===8?"    source = (D8Texture *)source_iface;":"    if (source->iface.lpVtbl != &g_texture_vtbl ||",api===8?"    if (((D8Texture*)source_iface)->iface.lpVtbl == &g_texture_vtbl && ((D8Texture*)source_iface)->width != ((D8Texture*)destination_iface)->width) return D3DERR_INVALIDCALL;\n    source = (D8Texture *)source_iface;":"    if (source->iface.lpVtbl == &g_texture_vtbl && source->width != destination->width) return D3DERR_INVALIDCALL;\n    if (source->iface.lpVtbl != &g_texture_vtbl ||"]),
];
function span(source,name){const m=new RegExp("(?:static )?(?:uint32_t|int|BOOL|HRESULT)(?: WINAPI)? "+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let at=source.indexOf("{",m.index)+1,depth=1;for(;depth;++at){assert(at<source.length);if(source[at]==="{")depth++;if(source[at]==="}")depth--;}return[m.index,at];}
try{for(const[label,kind,name,before,after]of cases){const file=kind==="plan"?root+"/runtime/d9wg_runtime.c":kind==="bridge"?root+"/runtime/d9wg_texture_copy_bridge.h":root+"/d3d"+kind+"proxy/d3d"+kind+"_proxy.c",source=fs.readFileSync(file,"utf8"),[start,end]=span(source,name),part=source.slice(start,end);assert.equal(part.split(before).length,2,label);const mutated=dir+(kind==="bridge"?"/bridge.h":"/source.c");fs.writeFileSync(mutated,source.slice(0,start)+part.replace(before,after)+source.slice(end));const test=kind==="plan"?"d9wg_texture_copy_plan_test.js":"d9wg_texture_copy_test.js",key=kind==="plan"?"D9WG_TEXTURE_COPY_RUNTIME_SOURCE":kind==="bridge"?"D9WG_TEXTURE_COPY_BRIDGE_HEADER":"D9WG_TEXTURE_COPY_FRONTEND_SOURCE",r=spawnSync(process.execPath,[path.join(__dirname,test)],{encoding:"utf8",env:{...process.env,[key]:mutated,...(typeof kind==="number"?{D9WG_TEXTURE_COPY_API:String(kind)}:{})}});assert.notEqual(r.status,0,"survived "+label);assert.match(r.stderr,/texture copy (?:plan|SDK) line/,"runtime assertion required "+label);}
 console.log("PASS texture copy mutations: "+cases.length+"/"+cases.length+"; mip selection/availability, face strides, source integrity, metadata preflight, allocation/copy order and actual SDK dimension acceptance");
}finally{fs.rmSync(dir,{recursive:true,force:true});}
