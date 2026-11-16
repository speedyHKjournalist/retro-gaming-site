"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'texture8-adapter-mut-')),source=fs.readFileSync(root+'/d3d8proxy/d3d8_core_cube_texture.c','utf8');
const plan=[
 ["GetCubeMapSurface loses face","core_cube_texture8_get_cube_map_surface","texture->backend, (UINT)face, level, &surface);","texture->backend, ((UINT)face+1)%6, level, &surface);"],
 ["Lock loses face","core_cube_texture8_lock","texture->backend, (UINT)face, level, locked,","texture->backend, ((UINT)face+1)%6, level, locked,"],
 ["Unlock loses face","core_cube_texture8_unlock","texture->backend, (UINT)face, level);","texture->backend, ((UINT)face+1)%6, level);"],
 ["Dirty loses face","core_cube_texture8_add_dirty","texture->backend, (UINT)face, rect);","texture->backend, ((UINT)face+1)%6, rect);"],

 ["borrowed getter wrong class","d8wg_core_cube_texture_backend8","((D8CoreCubeTexture *)texture)->identity != &g_core_cube_texture8_vtbl","FALSE"],
 ["borrowed getter returns no backend","d8wg_core_cube_texture_backend8","return ((D8CoreCubeTexture *)texture)->backend;","return NULL;"],
  [
    "QI stale output",
    "core_cube_texture8_query",
    "    *out = NULL;",
    "    /* stale */"
  ],
  [
    "QI null output success",
    "core_cube_texture8_query",
    "if (!out) return E_POINTER;",
    "if (!out) return S_OK;"
  ],
  [
    "QI no ref",
    "core_cube_texture8_query",
    "    core_cube_texture8_add_ref(iface);",
    "    (void)iface;"
  ],
  [
    "BaseTexture8 rejected",
    "core_cube_texture8_query",
    "&& !core_cube_texture8_guid(iid, &IID_IDirect3DBaseTexture8)",
    "&& TRUE"
  ],
  [
    "Texture8 rejected",
    "core_cube_texture8_query",
    "&& !core_cube_texture8_guid(iid, &IID_IDirect3DCubeTexture8)",
    "&& TRUE"
  ],
  [
    "AddRef omits common lifecycle",
    "core_cube_texture8_add_ref",
    "return d8wg_core_view_add8(&texture->refcount, (IUnknown *)texture->backend, texture->device);",
    "return 1; (void)texture;"
  ],
  [
    "Release omits common lifecycle",
    "core_cube_texture8_release",
    "return d8wg_core_view_release8(&texture->refcount, (IUnknown *)texture->backend, texture->device);",
    "return 0; (void)texture;"
  ],
  [
    "GetDevice no pin",
    "core_cube_texture8_get_device",
    "    IDirect3DDevice8_AddRef(*out);",
    "    /* no device ref */"
  ],
  [
    "GetDevice wrong invalid output",
    "core_cube_texture8_get_device",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) return E_POINTER;"
  ],
  [
    "SetPrivate size lost",
    "core_cube_texture8_set_private",
    "guid, data, size, flags);",
    "guid, data, size+1, flags);"
  ],
  [
    "SetPrivate flags lost",
    "core_cube_texture8_set_private",
    "guid, data, size, flags);",
    "guid, data, size, flags^1u);"
  ],
  [
    "GetPrivate size pointer lost",
    "core_cube_texture8_get_private",
    "guid, data, size);",
    "guid, data, NULL); (void)size;"
  ],
  [
    "FreePrivate GUID lost",
    "core_cube_texture8_free_private",
    "texture->backend, guid);",
    "texture->backend, NULL); (void)guid;"
  ],
  [
    "SetPriority lost",
    "core_cube_texture8_set_priority",
    "return texture->methods->set_priority(texture->backend, priority);",
    "return texture->methods->get_priority(texture->backend); (void)priority;"
  ],
  [
    "GetPriority wrong method",
    "core_cube_texture8_get_priority",
    "get_priority(texture->backend);",
    "get_lod(texture->backend);"
  ],
  [
    "Preload lost",
    "core_cube_texture8_preload",
    "    texture->methods->preload(texture->backend);",
    "    (void)texture;"
  ],
  [
    "GetType wrong dispatch",
    "core_cube_texture8_get_type",
    "get_type(texture->backend);",
    "get_level_count(texture->backend);"
  ],
  [
    "SetLOD request lost",
    "core_cube_texture8_set_lod",
    "texture->backend, lod);",
    "texture->backend, 0); (void)lod;"
  ],
  [
    "GetLOD wrong dispatch",
    "core_cube_texture8_get_lod",
    "get_lod(texture->backend);",
    "get_level_count(texture->backend);"
  ],
  [
    "GetLevelCount wrong dispatch",
    "core_cube_texture8_get_level_count",
    "get_level_count(texture->backend);",
    "get_type(texture->backend);"
  ],
  [
    "GetLevelDesc wrong level",
    "core_cube_texture8_get_level_desc",
    "texture->backend, level, out,",
    "texture->backend, level+1, out,"
  ],
  [
    "GetLevelDesc wrong bytes",
    "core_cube_texture8_get_level_desc",
    "out, sizeof(*out));",
    "out, sizeof(*out)-4);"
  ],
  [
    "GetSurface null output succeeds",
    "core_cube_texture8_get_cube_map_surface",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) return D3D_OK;"
  ],
  [
    "GetSurface stale error output",
    "core_cube_texture8_get_cube_map_surface",
    "    *out = NULL;",
    "    /* stale */"
  ],
  [
    "GetSurface wrong level",
    "core_cube_texture8_get_cube_map_surface",
    "texture->backend, (UINT)face, level, &surface);",
    "texture->backend, (UINT)face, level+1, &surface);"
  ],
  [
    "GetSurface provider status lost",
    "core_cube_texture8_get_cube_map_surface",
    "if (FAILED(hr)) return hr;",
    "if (FAILED(hr)) return D3D_OK;"
  ],
  [
    "GetSurface count not coupled",
    "core_cube_texture8_get_cube_map_surface",
    "(IUnknown *)iface, TRUE, out);",
    "(IUnknown *)iface, FALSE, out);"
  ],
  [
    "GetSurface container is device",
    "core_cube_texture8_get_cube_map_surface",
    "(IUnknown *)iface, TRUE, out);",
    "(IUnknown *)texture->device, TRUE, out);"
  ],
  [
    "Lock loses level",
    "core_cube_texture8_lock",
    "texture->backend, (UINT)face, level, locked,",
    "texture->backend, (UINT)face, level+1, locked,"
  ],
  [
    "Lock loses rect",
    "core_cube_texture8_lock",
    "locked, rect, flags);",
    "locked, NULL, flags); (void)rect;"
  ],
  [
    "Lock loses flags",
    "core_cube_texture8_lock",
    "locked, rect, flags);",
    "locked, rect, flags^1u);"
  ],
  [
    "Unlock loses level",
    "core_cube_texture8_unlock",
    "texture->backend, (UINT)face, level);",
    "texture->backend, (UINT)face, level+1);"
  ],
  [
    "Dirty loses rect",
    "core_cube_texture8_add_dirty",
    "texture->backend, (UINT)face, rect);",
    "texture->backend, (UINT)face, NULL); (void)rect;"
  ],
  [
    "Wrap output stale",
    "d8wg_core_wrap_cube_texture8",
    "    if (out) *out = NULL;",
    "    /* stale */"
  ],
  [
    "Wrap input not consumed",
    "d8wg_core_wrap_cube_texture8",
    "    if (owned) IUnknown_Release((IUnknown *)owned);",
    "    (void)owned;"
  ],
  [
    "Wrap candidate OOM wrong error",
    "d8wg_core_wrap_cube_texture8",
    "if (!view) { hr = E_OUTOFMEMORY; goto done; }",
    "if (!view) { hr = D3DERR_INVALIDCALL; goto done; }"
  ],
  [
    "Wrap candidate leaked",
    "d8wg_core_wrap_cube_texture8",
    "            HeapFree(GetProcessHeap(), 0, view);",
    "            /* leaked candidate */"
  ],
  [
    "Wrap race no winner succeeds",
    "d8wg_core_wrap_cube_texture8",
    "if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }",
    "if (!cached) { hr = D3D_OK; goto done; }"
  ],
  [
    "Wrap wrong device accepted",
    "d8wg_core_wrap_cube_texture8",
    "view->device != device",
    "FALSE"
  ],
  [
    "Wrap wrong cached class accepted",
    "d8wg_core_wrap_cube_texture8",
    "view->identity != &g_core_cube_texture8_vtbl",
    "FALSE"
  ],
  [
    "Hotcache allocates",
    "d8wg_core_wrap_cube_texture8",
    "    view = (D8CoreCubeTexture *)cached;",
    "    view = NULL;"
  ]
];
const common=fs.readFileSync(root+'/d3d8proxy/d3d8_core_view.h','utf8');
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to,kind]of plan){const input=kind?common:source;const[a,b]=span(input,fn),body=input.slice(a,b);assert(body.includes(from),name);const candidate=input.slice(0,a)+body.replace(from,to)+input.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_cube_texture_adapter_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,...(kind?{D8WG_CORE_VIEW_SOURCE:file}:{D8WG_CUBE_ADAPTER_SOURCE:file})}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Surface8 adapter line \d+:|Assertion failed: \([^\n]*\), function \w+, file adapter\.c, line \d+\.|adapter\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS SDK8 cube texture adapter mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
