"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'texture8-adapter-mut-')),source=fs.readFileSync(root+'/d3d8proxy/d3d8_core_volume.c','utf8');
const plan=[
  [
    "QI stale",
    "core_volume8_query",
    "    *out = NULL;",
    "    /* stale */"
  ],
  [
    "QI nullout",
    "core_volume8_query",
    "if (!out) return E_POINTER;",
    "if (!out) return S_OK;"
  ],
  [
    "QI no ref",
    "core_volume8_query",
    "    core_volume8_add_ref(iface);",
    "    (void)iface;"
  ],
  [
    "QI accepts Resource8",
    "core_volume8_query",
    "&& !core_volume8_guid(iid, &IID_IDirect3DVolume8))",
    "&& !core_volume8_guid(iid, &IID_IDirect3DVolume8) && !core_volume8_guid(iid, &IID_IDirect3DResource8))"
  ],
  [
    "AddRef no container",
    "core_volume8_add_ref",
    "return IUnknown_AddRef(((D8CoreVolume *)iface)->container);",
    "return 1; (void)iface;"
  ],
  [
    "Release no container",
    "core_volume8_release",
    "return IUnknown_Release(((D8CoreVolume *)iface)->container);",
    "return 0; (void)iface;"
  ],
  [
    "GetDevice no ref",
    "core_volume8_get_device",
    "    IDirect3DDevice8_AddRef(*out);",
    "    /* no ref */"
  ],
  [
    "GetDevice invalid out",
    "core_volume8_get_device",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) return E_POINTER;"
  ],
  [
    "Private size",
    "core_volume8_set_private",
    "guid, data, size, flags);",
    "guid, data, size+1, flags);"
  ],
  [
    "Private flags",
    "core_volume8_set_private",
    "guid, data, size, flags);",
    "guid, data, size, flags^1u);"
  ],
  [
    "Private sizeptr",
    "core_volume8_get_private",
    "guid, data, size);",
    "guid, data, NULL); (void)size;"
  ],
  [
    "Private GUID",
    "core_volume8_free_private",
    "surface->backend, guid);",
    "surface->backend, NULL); (void)guid;"
  ],
  [
    "Container invalid out",
    "core_volume8_get_container",
    "if (!out) return E_POINTER;",
    "if (!out) return S_OK;"
  ],
  [
    "Container stale",
    "core_volume8_get_container",
    "    *out = NULL;",
    "    /* stale */"
  ],
  [
    "Container IID lost",
    "core_volume8_get_container",
    "->container, iid, out);",
    "->container, &IID_IUnknown, out);"
  ],
  [
    "Desc bytes",
    "core_volume8_get_desc",
    "out, sizeof(*out));",
    "out, sizeof(*out)-4);"
  ],
  [
    "Lock flags",
    "core_volume8_lock",
    "locked, rect, flags);",
    "locked, rect, flags^1u);"
  ],
  [
    "Lock box",
    "core_volume8_lock",
    "locked, rect, flags);",
    "locked, NULL, flags); (void)rect;"
  ],
  [
    "Unlock status",
    "core_volume8_unlock",
    "return surface->methods->unlock_box(surface->backend);",
    "surface->methods->unlock_box(surface->backend); return D3D_OK;"
  ],
  [
    "Wrap stale",
    "d8wg_core_wrap_volume8",
    "    if (out) *out = NULL;",
    "    /* stale */"
  ],
  [
    "Wrap owned not consumed",
    "d8wg_core_wrap_volume8",
    "    if (owned) IUnknown_Release((IUnknown *)owned);",
    "    (void)owned;"
  ],
  [
    "Wrap OOM status",
    "d8wg_core_wrap_volume8",
    "if (!view) { hr = E_OUTOFMEMORY; goto done; }",
    "if (!view) { hr = D3DERR_INVALIDCALL; goto done; }"
  ],
  [
    "Wrap candidate leaked",
    "d8wg_core_wrap_volume8",
    "            HeapFree(GetProcessHeap(), 0, view);",
    "            /* leaked */"
  ],
  [
    "Wrap race missing winner",
    "d8wg_core_wrap_volume8",
    "if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }",
    "if (!cached) { hr = D3D_OK; goto done; }"
  ],
  [
    "Wrap cached device",
    "d8wg_core_wrap_volume8",
    "view->device != device",
    "FALSE"
  ],
  [
    "Wrap cached container",
    "d8wg_core_wrap_volume8",
    "view->container != container",
    "FALSE"
  ],
  [
    "Wrap cached vtbl",
    "d8wg_core_wrap_volume8",
    "view->iface.lpVtbl != &g_core_volume8_vtbl",
    "FALSE"
  ],
  [
    "Wrap allocates hotcache",
    "d8wg_core_wrap_volume8",
    "    view = (D8CoreVolume *)cached;",
    "    view = NULL;"
  ]
];
const common=fs.readFileSync(root+'/d3d8proxy/d3d8_core_view.h','utf8');
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to,kind]of plan){const input=kind?common:source;const[a,b]=span(input,fn),body=input.slice(a,b);assert(body.includes(from),name);const candidate=input.slice(0,a)+body.replace(from,to)+input.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_volume_texture_adapter_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,...(kind?{D8WG_CORE_VIEW_SOURCE:file}:{D8WG_VOLUME_ADAPTER_SOURCE:file})}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Surface8 adapter line \d+:|Assertion failed: \([^\n]*\), function \w+, file adapter\.c, line \d+\.|adapter\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS SDK8 volume child adapter mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
