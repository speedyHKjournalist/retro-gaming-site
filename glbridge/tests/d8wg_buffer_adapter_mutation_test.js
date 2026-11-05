"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'buffer8-adapter-mut-')),source=fs.readFileSync(root+'/d3d8proxy/d3d8_core_buffer.c','utf8');
const plan=[
  [
    "QI stale",
    "core_buffer8_query",
    "    *out = NULL;",
    "    /* stale */"
  ],
  [
    "QI no ref",
    "core_buffer8_query",
    "    d8wg_core_view_add8(&buffer->refcount, buffer->backend, buffer->device);",
    "    /* no ref */"
  ],
  [
    "QI wrong nullout",
    "core_buffer8_query",
    "if (!out) return E_POINTER;",
    "if (!out) return S_OK;"
  ],
  [
    "QI Resource8 rejected",
    "core_buffer8_query",
    "&& !d8wg_core_guid8(iid, &IID_IDirect3DResource8)",
    "&& TRUE"
  ],
  [
    "QI wrong class id",
    "core_buffer8_query",
    ": &IID_IDirect3DIndexBuffer8;",
    ": &IID_IDirect3DVertexBuffer8;"
  ],
  [
    "AddRef lost",
    "P##_add_ref",
    "return d8wg_core_view_add8(&b->refcount, b->backend, b->device);",
    "return 1; (void)b;"
  ],
  [
    "Release lost",
    "P##_release",
    "return d8wg_core_view_release8(&b->refcount, b->backend, b->device);",
    "return 0; (void)b;"
  ],
  [
    "GetDevice no pin",
    "P##_get_device",
    "IDirect3DDevice8_AddRef(*out);",
    "/* no pin */"
  ],
  [
    "GetDevice wrong nullout",
    "P##_get_device",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) return E_POINTER;"
  ],
  [
    "SetPrivate wrong bytes",
    "P##_set_private",
    "id, data, size, flags);",
    "id, data, size+1, flags);"
  ],
  [
    "SetPrivate wrong flags",
    "P##_set_private",
    "id, data, size, flags);",
    "id, data, size, flags^1u);"
  ],
  [
    "GetPrivate sizeptr lost",
    "P##_get_private",
    "id, data, size);",
    "id, data, NULL); (void)size;"
  ],
  [
    "FreePrivate GUID lost",
    "P##_free_private",
    "b->backend, id);",
    "b->backend, NULL); (void)id;"
  ],
  [
    "SetPriority wrong value",
    "P##_set_priority",
    "b->backend, value);",
    "b->backend, value^1u);"
  ],
  [
    "GetPriority wrong method",
    "P##_get_priority",
    "get_priority(b->backend);",
    "get_type(b->backend);"
  ],
  [
    "PreLoad omitted",
    "P##_preload",
    "b->methods->preload(b->backend);",
    "(void)b;"
  ],
  [
    "GetType wrong method",
    "P##_get_type",
    "get_type(b->backend);",
    "get_priority(b->backend);"
  ],
  [
    "Lock offset lost",
    "P##_lock",
    "b->backend, offset, size, out, flags);",
    "b->backend, offset+1, size, out, flags);"
  ],
  [
    "Lock size lost",
    "P##_lock",
    "b->backend, offset, size, out, flags);",
    "b->backend, offset, size+1, out, flags);"
  ],
  [
    "Lock output lost",
    "P##_lock",
    "b->backend, offset, size, out, flags);",
    "b->backend, offset, size, NULL, flags); (void)out;"
  ],
  [
    "Lock flags lost",
    "P##_lock",
    "b->backend, offset, size, out, flags);",
    "b->backend, offset, size, out, flags^1u);"
  ],
  [
    "Unlock status lost",
    "P##_unlock",
    "return b->methods->unlock(b->backend);",
    "b->methods->unlock(b->backend); return D3D_OK;"
  ],
  [
    "Descriptor wrong size",
    "P##_get_desc",
    "out, sizeof(*out));",
    "out, sizeof(*out)-4);"
  ],
  [
    "Wrap owned not consumed",
    "core_buffer8_wrap",
    "    if (owned) IUnknown_Release(owned);",
    "    (void)owned;"
  ],
  [
    "Wrap OOM wrong status",
    "core_buffer8_wrap",
    "if (!view) { hr = E_OUTOFMEMORY; goto done; }",
    "if (!view) { hr = D3DERR_INVALIDCALL; goto done; }"
  ],
  [
    "Wrap leaked candidate",
    "core_buffer8_wrap",
    "            HeapFree(GetProcessHeap(), 0, view);",
    "            /* leaked */"
  ],
  [
    "Wrap race missing winner",
    "core_buffer8_wrap",
    "if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }",
    "if (!cached) { hr = D3D_OK; goto done; }"
  ],
  [
    "Wrap wrong device",
    "core_buffer8_wrap",
    "view->device != device",
    "FALSE"
  ],
  [
    "Wrap wrong class",
    "core_buffer8_wrap",
    "view->iface.lpVtbl != core_buffer8_vtable(kind)",
    "FALSE"
  ],
  [
    "Wrap wrong kind",
    "core_buffer8_wrap",
    "view->kind != kind",
    "FALSE"
  ],
  [
    "Wrap hotcache allocates",
    "core_buffer8_wrap",
    "\n    view = (D8CoreBuffer *)cached;",
    "\n    view = NULL;"
  ],
  [
    "Wrap no pin",
    "core_buffer8_wrap",
    "    d8wg_core_view_add8(&view->refcount, view->backend, view->device);",
    "    /* no pin */"
  ],
  [
    "Vertex wrap wrong kind",
    "d8wg_core_wrap_vertex_buffer8",
    "device, 6u,",
    "device, 7u,"
  ],
  [
    "Index wrap wrong kind",
    "d8wg_core_wrap_index_buffer8",
    "device, 7u,",
    "device, 6u,"
  ],
  [
    "Borrowed wrong class",
    "core_buffer8_backend",
    "iface->lpVtbl != core_buffer8_vtable(kind)",
    "((void)kind, FALSE)"
  ],
  [
    "Borrowed missing backend",
    "core_buffer8_backend",
    "return ((D8CoreBuffer *)iface)->backend;",
    "return NULL;"
  ]
];
function span(s,name){const at=s.indexOf(name+'(');assert(at>=0,name);const a=s.lastIndexOf('\n',at)+1;let b=s.indexOf('{',at)+1,d=1;for(;d;b++){if(s[b]==='{')d++;if(s[b]==='}')d--;}return[a,b];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);assert.equal(body.split(from).length,2,name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_buffer_adapter_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_BUFFER_ADAPTER_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Surface8 adapter line \d+:|Assertion failed: \([^\n]*\), function \w+, file adapter\.c, line \d+\.|adapter\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS SDK8 VB8/IB8 adapter mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
