"use strict";
const fs=require("node:fs"),os=require("node:os"),path=require("node:path"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.resolve(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-private-mut-"));
const cases=[
 ["GUID suffix ignored","guid_equal","if (left->Data4[index] != right->Data4[index])","if (0 && left->Data4[index] != right->Data4[index])"],
 ["byte payload not copied","surface_SetPrivateData","else if (bytes) CopyMemory(entry->data, data, bytes);","else if (0 && bytes) CopyMemory(entry->data, data, bytes);"],
 ["stored COM object not retained","surface_SetPrivateData","IUnknown_AddRef((IUnknown *)data);","if (0) IUnknown_AddRef((IUnknown *)data);"],
 ["stored COM object not released","surface_private_data_release","IUnknown_Release(object);","if (0) IUnknown_Release(object);"],
 ["getter adds an unsupported COM ref","surface_GetPrivateData","if (entry->bytes) CopyMemory(buffer, entry->data, entry->bytes);","if (entry->flags & DDSPD_IUNKNOWNPOINTER) { IUnknown *object; CopyMemory(&object, entry->data, sizeof(object)); IUnknown_AddRef(object); } if (entry->bytes) CopyMemory(buffer, entry->data, entry->bytes);"],
 ["small buffer accepted","surface_GetPrivateData","if (*bytes < entry->bytes)","if (0 && *bytes < entry->bytes)"],
 ["NULL buffer beats MORE_DATA","surface_GetPrivateData","if (*bytes < entry->bytes)","if (!buffer) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); if (*bytes < entry->bytes)"],
 ["bad nonzero object size preserves old entry","surface_SetPrivateData","if (bytes && *link)","if (0 && bytes && *link)"],
 ["replacement published after reentrant Release","surface_SetPrivateData","*link = entry;\n    surface_private_data_release(old);","surface_private_data_release(old);\n    *link = entry;"],
 ["Free leaves entry visible during Release","surface_FreePrivateData","*link = entry->next;\n    surface_private_data_release(entry);","surface_private_data_release(entry);\n    *link = NULL;"],
 ["allocation failure erases old entry","surface_SetPrivateData","if (!entry) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);","if (!entry) { old = *link; *link = old ? old->next : NULL; surface_private_data_release(old); return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY); }"],
 ["volatile expiry disabled","surface_GetPrivateData","if ((entry->flags & DDSPD_VOLATILE) &&","if (0 && (entry->flags & DDSPD_VOLATILE) &&"],
 ["expired size preserved","surface_GetPrivateData","*bytes = 0;","/* missing native zero */"],
 ["uniqueness value wraps to zero","surface_memory_changed","if (!InterlockedIncrement((LONG *)&memory->content_version))\n        InterlockedIncrement((LONG *)&memory->content_version);","InterlockedIncrement((LONG *)&memory->content_version);"],
];
function span(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}return[m.index,end];}
try{for(const[label,name,from,to]of cases){const[start,end]=span(name),chunk=source.slice(start,end);assert(chunk.includes(from),label);const file=dir+"/mutant.c";fs.writeFileSync(file,source.slice(0,start)+chunk.replace(from,to)+source.slice(end));const r=spawnSync(process.execPath,[__dirname+"/ddraw_private_data_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_PRIVATE_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/private data line|AddressSanitizer|UndefinedBehaviorSanitizer/,label+": must compile then fail runtime checks\n"+r.stderr.slice(-1600));assert(!/:\d+:\d+: (?:fatal )?error:/.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS surface private-data mutations: "+cases.length+"/"+cases.length+" compiled implementations rejected");}finally{fs.rmSync(dir,{recursive:true,force:true});}
