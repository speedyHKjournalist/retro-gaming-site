"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"backend8-buffers-mut-"));
function body(s,n){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+n+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,n);let b=s.indexOf("{",m.index)+1,d=1;for(;d;b++){if(s[b]==="{")d++;if(s[b]==="}")d--;}return[m.index,b,s.slice(m.index,b)];}
const cases=[
 ["vertex shared refusal", "device_create_vertex_buffer", "if (shared_handle)", "if (0 && shared_handle)"],
 ["vertex scratch refusal", "device_create_vertex_buffer", "pool >= D3DPOOL_SCRATCH", "pool > D3DPOOL_SCRATCH"],
 ["vertex aligned shadow", "device_create_vertex_buffer", "& ~(uintptr_t)15u", "& ~(uintptr_t)0u"],
 ["index shared refusal", "device_create_index_buffer", "if (shared_handle)", "if (0 && shared_handle)"],
 ["index scratch refusal", "device_create_index_buffer", "pool >= D3DPOOL_SCRATCH", "pool > D3DPOOL_SCRATCH"],
 ["index aligned shadow", "device_create_index_buffer", "& ~(uintptr_t)15u", "& ~(uintptr_t)0u"],
 ["VB profile","d9wg_d3d9_core_vertex_buffer8","!= 8u","!= 9u"],
 ["IB profile","d9wg_d3d9_core_index_buffer8","!= 8u","!= 9u"],
 ["VB failed output","d9wg_d3d9_core_vertex_buffer8","*out = NULL;","(void)out;"],
 ["IB failed output","d9wg_d3d9_core_index_buffer8","*out = NULL;","(void)out;"],
 ["VB length","d9wg_d3d9_core_vertex_buffer8","iface, length, usage, fvf,","iface, length + 1u, usage, fvf,"],
 ["IB length","d9wg_d3d9_core_index_buffer8","iface, length, usage,","iface, length + 1u, usage,"],
 ["VB usage","d9wg_d3d9_core_vertex_buffer8","length, usage, fvf,","length, usage ^ 8u, fvf,"],
 ["IB usage","d9wg_d3d9_core_index_buffer8","length, usage,","length, usage ^ 8u,"],
 ["VB FVF","d9wg_d3d9_core_vertex_buffer8","usage, fvf,","usage, fvf ^ 1u,"],
 ["IB format","d9wg_d3d9_core_index_buffer8","(D3DFORMAT)format,","(D3DFORMAT)(format ^ 1u),"],
 ["VB pool","d9wg_d3d9_core_vertex_buffer8","(D3DPOOL)pool,","(D3DPOOL)(pool ^ 1u),"],
 ["IB pool","d9wg_d3d9_core_index_buffer8","(D3DPOOL)pool,","(D3DPOOL)(pool ^ 1u),"],
 ["VB sharedNULL","d9wg_d3d9_core_vertex_buffer8","out, NULL);","out, (HANDLE *)1);"],
 ["IB sharedNULL","d9wg_d3d9_core_index_buffer8","out, NULL);","out, (HANDLE *)1);"],
 ["IB alignment","device_create_index_buffer","if (length % index_size)","if (length % index_size && 0)"],
 ["VB list","device_create_vertex_buffer","device->vertex_buffers = buffer;","(void)buffer;"],
 ["IB list","device_create_index_buffer","device->index_buffers = buffer;","(void)buffer;"],
 ["VB initial state","device_create_vertex_buffer","HEAP_ZERO_MEMORY,\n            sizeof(*buffer)","0,\n            sizeof(*buffer)"],
 ["IB initial state","device_create_index_buffer","HEAP_ZERO_MEMORY,\n            sizeof(*buffer)","0,\n            sizeof(*buffer)"],
 ["VB zero storage","device_create_vertex_buffer","HEAP_ZERO_MEMORY, (SIZE_T)length + 15u","0, (SIZE_T)length + 15u"],
 ["IB zero storage","device_create_index_buffer","HEAP_ZERO_MEMORY, (SIZE_T)length + 15u","0, (SIZE_T)length + 15u"],
 ["VB descriptor FVF","vb_get_desc","desc->FVF = buffer->fvf;","desc->FVF = 0;"],
 ["IB descriptor size","ib_get_desc","desc->Size = buffer->length;","desc->Size = 0;"],
 ["VB descriptor size","vb_get_desc","desc->Size = buffer->length;","desc->Size = 0;"]
];
try{for(const[label,name,from,to]of cases){const[a,b,f]=body(source,name);assert(f.includes(from),label);const file=dir+"/frontend.c";fs.writeFileSync(file,source.slice(0,a)+f.replace(from,to)+source.slice(b));const test="d8wg_backend_buffers_test.js",r=spawnSync(process.execPath,[root+"/tests/"+test],{encoding:"utf8",timeout:180000,env:{...process.env,D8WG_BACKEND_BUFFERS_SOURCE:file}}),out=r.stdout+r.stderr;assert(r.status!==0&&!r.signal,label+" survived/abnormal "+out);assert(!/error:|undefined reference|error generated|errors generated/.test(out),label+" compiler failure is not a runtime mutation kill: "+out);assert(/Backend8 buffers case \d+ line \d+:/.test(out),label+" did not fail actual C semantic assertion: "+out);}
 console.log(`PASS backend8 buffers mutations: ${cases.length}/${cases.length};strict compilation then actual constructor/lock/upload runtime assertions,profile/length/usage/FVF/format/pool/sharedNULL/output/list/zero-storage/descriptor guards`);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
