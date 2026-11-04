"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'surface8-adapter-mut-')),source=fs.readFileSync(root+'/d3d8proxy/d3d8_core_surface.c','utf8');
const plan=[
 ["GUID trailing byte ignored","core_surface8_guid","i < 8","i < 7"],
 ["cached wrong class accepted","d8wg_core_wrap_surface8","view->iface.lpVtbl != &g_core_surface8_vtbl","FALSE"],
  [
    "coupled AddRef result",
    "core_surface8_add_ref",
    "return IUnknown_AddRef(surface->container);",
    "return IUnknown_AddRef(surface->container)+1;"
  ],
  [
    "coupled Release result",
    "core_surface8_release",
    "return IUnknown_Release(surface->container);",
    "return IUnknown_Release(surface->container)+1;"
  ],
  [
    "own core pin omitted",
    "core_surface8_add_ref",
    "        IUnknown_AddRef((IUnknown *)surface->backend);",
    "        /* lost core pin */"
  ],
  [
    "own device pin omitted",
    "core_surface8_add_ref",
    "        IDirect3DDevice8_AddRef(surface->device);",
    "        /* lost device pin */"
  ],
  [
    "own repeated AddRef pins core",
    "core_surface8_add_ref",
    "    if (refs == 1) {",
    "    if (refs >= 1) {"
  ],
  [
    "release backend lost",
    "core_surface8_release",
    "        IUnknown_Release(backend);",
    "        (void)backend;"
  ],
  [
    "release device lost",
    "core_surface8_release",
    "        IDirect3DDevice8_Release(device);",
    "        (void)device;"
  ],
  [
    "QI output not cleared",
    "core_surface8_query",
    "    *out = NULL;",
    "    /* stale output */"
  ],
  [
    "QI null output succeeds",
    "core_surface8_query",
    "if (!out) return E_POINTER;",
    "if (!out) return S_OK;"
  ],
  [
    "QI refs omitted",
    "core_surface8_query",
    "    core_surface8_add_ref(iface);",
    "    (void)iface;"
  ],
  [
    "QI Surface8 refused",
    "core_surface8_query",
    "&& !core_surface8_guid(iid, &IID_IDirect3DSurface8)",
    "&& TRUE"
  ],
  [
    "GetDevice refs omitted",
    "core_surface8_get_device",
    "    IDirect3DDevice8_AddRef(*out);",
    "    /* missing device ref */"
  ],
  [
    "GetDevice null status",
    "core_surface8_get_device",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) return E_POINTER;"
  ],
  [
    "GetContainer ignores iid",
    "core_surface8_get_container",
    "->container, iid, out);",
    "->container, &IID_IUnknown, out);"
  ],
  [
    "GetContainer null output succeeds",
    "core_surface8_get_container",
    "if (!out) return E_POINTER;",
    "if (!out) return S_OK;"
  ],
  [
    "GetDesc wrong bytes",
    "core_surface8_get_desc",
    "out, sizeof(*out));",
    "out, sizeof(*out)-4);"
  ],
  [
    "SetPrivate loses size",
    "core_surface8_set_private",
    "guid, data, size, flags);",
    "guid, data, size+1, flags);"
  ],
  [
    "SetPrivate loses flags",
    "core_surface8_set_private",
    "guid, data, size, flags);",
    "guid, data, size, flags^1u);"
  ],
  [
    "GetPrivate loses size output",
    "core_surface8_get_private",
    "guid, data, size);",
    "guid, data, NULL); (void)size;"
  ],
  [
    "FreePrivate loses GUID",
    "core_surface8_free_private",
    "surface->backend, guid);",
    "surface->backend, NULL); (void)guid;"
  ],
  [
    "Lock loses RECT",
    "core_surface8_lock",
    "locked, rect, flags);",
    "locked, NULL, flags); (void)rect;"
  ],
  [
    "Lock loses flags",
    "core_surface8_lock",
    "locked, rect, flags);",
    "locked, rect, flags^1u);"
  ],
  [
    "Unlock swallows error",
    "core_surface8_unlock",
    "return surface->methods->unlock_rect(surface->backend);",
    "surface->methods->unlock_rect(surface->backend);return D3D_OK;"
  ],
  [
    "owned input not consumed",
    "d8wg_core_wrap_surface8",
    "    if (owned) IUnknown_Release((IUnknown *)owned);",
    "    (void)owned;"
  ],
  [
    "failure output stale",
    "d8wg_core_wrap_surface8",
    "    if (out) *out = NULL;",
    "    /* stale output */"
  ],
  [
    "allocation error normalized",
    "d8wg_core_wrap_surface8",
    "if (!view) { hr = E_OUTOFMEMORY; goto done; }",
    "if (!view) { hr = D3DERR_INVALIDCALL; goto done; }"
  ],
  [
    "race candidate leaked",
    "d8wg_core_wrap_surface8",
    "            HeapFree(GetProcessHeap(), 0, view);",
    "            /* lost candidate */"
  ],
  [
    "race missing winner succeeds",
    "d8wg_core_wrap_surface8",
    "if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }",
    "if (!cached) { hr = D3D_OK; goto done; }"
  ],
  [
    "wrong frontend device accepted",
    "d8wg_core_wrap_surface8",
    "view->device != device",
    "FALSE"
  ],
  [
    "wrong frontend container accepted",
    "d8wg_core_wrap_surface8",
    "view->container != container",
    "FALSE"
  ],
  [
    "wrong coupling accepted",
    "d8wg_core_wrap_surface8",
    "view->container_refcount != !!container_refcount",
    "FALSE"
  ],
  [
    "hot cache allocates",
    "d8wg_core_wrap_surface8",
    "    view = (D8CoreSurface *)cached;",
    "    view = NULL;"
  ]
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_surface_adapter_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_SURFACE_ADAPTER_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Surface8 adapter line \d+:|Assertion failed: \([^\n]*\), function \w+, file adapter\.c, line \d+\.|adapter\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS SDK8 surface adapter mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
