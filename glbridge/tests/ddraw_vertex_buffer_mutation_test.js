"use strict";
const fs=require("node:fs"),os=require("node:os"),path=require("node:path"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),sources={seven:fs.readFileSync(root+'/ddrawproxy/d3d7_proxy.inc','utf8'),legacy:fs.readFileSync(root+'/ddrawproxy/d3d_legacy_proxy.inc','utf8')},dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-vb-mut-'));
const cases=[
 ['legacy forwards v7 lock hints','seven','d3d7_vb_Lock','if (buffer->creation_version < DD_VERSION_7)','if (FALSE && buffer->creation_version < DD_VERSION_7)'],
 ['v7 lock hints are discarded','seven','d3d7_vb_Lock','if (buffer->creation_version < DD_VERSION_7)','if (TRUE)'],
 ['repeated mapping rejected','seven','d3d7_vb_Lock','if (buffer->locked) {','if (buffer->locked) { return DDWG_TRACE_ERROR(D3DERR_VERTEXBUFFERLOCKED);'],
 ['late readonly suppresses prior write','seven','d3d7_vb_Lock','((buffer->lock_flags & flags) & unanimous)','(flags & unanimous)'],
 ['mixed lock grants NOOVERWRITE','seven','d3d7_vb_Lock','DWORD unanimous = DDLOCK_READONLY | DDLOCK_NOOVERWRITE;','DWORD unanimous = DDLOCK_READONLY;'],
 ['extra Unlock emits update','seven','d3d7_vb_Unlock','if (!buffer->locked) return D3D_OK;','if (FALSE && !buffer->locked) return D3D_OK;'],
 ['discard erases native backing bytes','seven','d3d7_vb_Lock','d9wg_clip_buffer_free(&buffer->clips, d9wg_vertex_windows_clip_release, NULL);','d9wg_clip_buffer_free(&buffer->clips, d9wg_vertex_windows_clip_release, NULL); ZeroMemory(buffer->shadow, buffer->byte_count);'],
 ['legacy factory uses v7 profile','legacy','d3d3_CreateVertexBuffer','flags, DD_VERSION_3);','flags, DD_VERSION_7);'],
 ['legacy IUnknown returns v7 pointer','seven','d3d7_vb_QueryInterface','buffer->creation_version == DD_VERSION_7\n            ?','1\n            ?'],
 ['legacy exposes v7 IID','seven','d3d7_vb_QueryInterface','else if (buffer->creation_version == DD_VERSION_7 &&','else if (1 &&'],
 ['v7 exposes legacy IID','seven','d3d7_vb_QueryInterface','else if (buffer->creation_version != DD_VERSION_7 &&','else if (1 &&'],
 ['legacy pins parent publicly','seven','d3d7_CreateVertexBufferForVersion','buffer->owner_public_ref = version == DD_VERSION_7;','buffer->owner_public_ref = 1;'],
 ['v7 loses public parent pin','seven','d3d7_CreateVertexBufferForVersion','buffer->owner_public_ref = version == DD_VERSION_7;','buffer->owner_public_ref = 0;'],
 ['destructor releases private parent publicly','seven','d3d7_vb_Release','ddraw_child_detach(owner, public_ref);','ddraw_child_detach(owner, !public_ref);'],
 ['failed ensure leaks parent pin','seven','d3d7_CreateVertexBufferForVersion','if (!ensure_device(owner)) {\n        ddraw_child_detach(owner, buffer->owner_public_ref);','if (!ensure_device(owner)) {\n        /* missing unwind */'],
 ['overflow status prevents native retry','seven','d3d7_CreateVertexBufferForVersion','return DDWG_TRACE_ERROR(D3DERR_TOOMANYVERTICES);','return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);'],
 ['valid large buffers newly capped','seven','d3d7_CreateVertexBufferForVersion','if (!d3d7_multiply_u32(description->dwNumVertices, layout.stride,','if (description->dwNumVertices > 65535) return DDWG_TRACE_ERROR(D3DERR_TOOMANYVERTICES); if (!d3d7_multiply_u32(description->dwNumVertices, layout.stride,'],
 ['descriptor wrongly gates caller size','seven','d3d7_vb_GetVertexBufferDesc','if (!description)','if (!description || description->dwSize != sizeof(*description))'],
 ['descriptor clobbers caller size','seven','d3d7_vb_GetVertexBufferDesc','description->dwCaps = stored->dwCaps;','description->dwSize = stored->dwSize; description->dwCaps = stored->dwCaps;'],
 ['descriptor loses original FVF','seven','d3d7_vb_GetVertexBufferDesc','description->dwFVF = stored->dwFVF;','description->dwFVF = 0;'],
 ['QI takes extra parent reference','seven','d3d7_vb_QueryInterface','IDirect3DVertexBuffer7_AddRef(iface);','ddraw_AddRef((IDirectDraw7 *)&buffer->owner->vtbl7); IDirect3DVertexBuffer7_AddRef(iface);'],
];
function span(s,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(s);assert(m,name);let end=s.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(s[end]==='{')depth++;if(s[end]==='}')depth--;}return[m.index,end];}
try{for(const[label,kind,name,from,to]of cases){const s=sources[kind],[start,end]=span(s,name),chunk=s.slice(start,end);assert(chunk.includes(from),label);const file=dir+'/mutant.inc';fs.writeFileSync(file,s.slice(0,start)+chunk.replace(from,to)+s.slice(end));const r=spawnSync(process.execPath,[__dirname+'/ddraw_vertex_buffer_test.js'],{encoding:'utf8',env:{...process.env,[kind==='seven'?'DDRAW_VB7_SOURCE':'DDRAW_VB_LEGACY_SOURCE']:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/vertex buffer line|AddressSanitizer|UndefinedBehaviorSanitizer/,label+': must compile then fail runtime checks\n'+r.stderr.slice(-1700));assert(!/:\d+:\d+: (?:fatal )?error:/.test(r.stderr),r.stderr);console.log('KILL '+label);}console.log('PASS vertex-buffer mutations: '+cases.length+'/'+cases.length+' compiled implementations rejected');}finally{fs.rmSync(dir,{recursive:true,force:true});}
