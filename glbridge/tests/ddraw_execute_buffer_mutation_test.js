"use strict";
const fs=require("node:fs"),os=require("node:os"),path=require("node:path"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.resolve(__dirname,"../ddrawproxy/d3d_legacy_proxy.inc"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-execute-mut-"));
const cases=[
 ["setter rejects opaque metadata","d3d_execute_buffer_SetExecuteData","if (!data || data->dwSize != sizeof(*data))","if (!data || data->dwSize != sizeof(*data) || (uint64_t)data->dwInstructionOffset + data->dwInstructionLength > buffer->byte_count)"],
 ["setter loses advisory count","d3d_execute_buffer_SetExecuteData","buffer->data = *data;","buffer->data = *data; buffer->data.dwVertexCount = 0;"],
 ["getter trusts caller size","d3d_execute_buffer_GetExecuteData","if (!data)","if (!data || data->dwSize != sizeof(*data))"],
 ["instruction bounds removed","d3d_execute_validate_internal","if ((uint64_t)cursor + buffer->data.dwInstructionLength > buffer->byte_count)","if (FALSE)"],
 ["malformed instruction stride accepted","d3d_execute_validate_internal","instruction->bSize < expected || next > end","FALSE"],
 ["advisory count gates processing","d3d_execute_process_vertices","if (source_end > buffer->byte_count)","if (source_end > buffer->byte_count || (uint64_t)process->wStart + process->dwCount > buffer->data.dwVertexCount)"],
 ["source bounds removed","d3d_execute_process_vertices","if (source_end > buffer->byte_count)","if (FALSE && source_end > buffer->byte_count)"],
 ["growing output loses prefix","d3d_execute_reserve_vertices","if (buffer->processed_count)","if (FALSE && buffer->processed_count)"],
 ["failed allocation commits count","d3d_execute_reserve_vertices","if (!processed) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);","if (!processed) { buffer->processed_count = (DWORD)count; return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY); }"],
 ["old output allocation leaked","d3d_execute_reserve_vertices","heap_free(buffer->processed);","if (FALSE) heap_free(buffer->processed);"],
 ["live vertex copy truncated","d3d_execute_process_vertices","(SIZE_T)process->dwCount * sizeof(D3DTLVERTEX)","sizeof(D3DTLVERTEX)"],
 ["invalid operation accepted","d3d_execute_process_vertices","if (operation != D3DPROCESSVERTICES_COPY &&","if (FALSE && operation != D3DPROCESSVERTICES_COPY &&"],
];
function span(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return[m.index,end];}
try{for(const[label,name,from,to]of cases){const[start,end]=span(name),chunk=source.slice(start,end);assert(chunk.includes(from),label);const file=dir+"/mutant.inc";fs.writeFileSync(file,source.slice(0,start)+chunk.replace(from,to)+source.slice(end));const r=spawnSync(process.execPath,[__dirname+"/ddraw_execute_buffer_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_EXECUTE_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/execute line|AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:/,label+": must compile then fail runtime checks\n"+r.stderr.slice(-2000));assert(!/:\d+:\d+: (?:fatal )?error:/.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS execute-buffer mutations: "+cases.length+"/"+cases.length+" compiled implementations rejected");}finally{fs.rmSync(dir,{recursive:true,force:true});}
