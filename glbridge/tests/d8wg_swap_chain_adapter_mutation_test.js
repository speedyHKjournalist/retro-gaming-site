"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'texture8-adapter-mut-')),source=fs.readFileSync(root+'/d3d8proxy/d3d8_core_swap_chain.c','utf8');
const plan=[
  [
    "QI stale",
    "core_swap_chain8_query",
    "    *out = NULL;",
    "    /* stale */"
  ],
  [
    "QI nullout success",
    "core_swap_chain8_query",
    "if (!out) return E_POINTER;",
    "if (!out) return S_OK;"
  ],
  [
    "QI no ref",
    "core_swap_chain8_query",
    "    core_swap_chain8_add_ref(iface);",
    "    (void)iface;"
  ],
  [
    "QI rejects class",
    "core_swap_chain8_query",
    "&& !core_swap_chain8_guid(iid, &IID_IDirect3DSwapChain8)",
    "&& TRUE"
  ],
  [
    "AddRef lost",
    "core_swap_chain8_add_ref",
    "return d8wg_core_view_add8(&texture->refcount, (IUnknown *)texture->backend, texture->device);",
    "return 1; (void)texture;"
  ],
  [
    "Release lost",
    "core_swap_chain8_release",
    "return d8wg_core_view_release8(&texture->refcount, (IUnknown *)texture->backend, texture->device);",
    "return 0; (void)texture;"
  ],
  [
    "Present source lost",
    "core_swap_chain8_present",
    "chain->backend, source, destination, window, dirty);",
    "chain->backend, NULL, destination, window, dirty); (void)source;"
  ],
  [
    "Present destination lost",
    "core_swap_chain8_present",
    "chain->backend, source, destination, window, dirty);",
    "chain->backend, source, NULL, window, dirty); (void)destination;"
  ],
  [
    "Present window lost",
    "core_swap_chain8_present",
    "chain->backend, source, destination, window, dirty);",
    "chain->backend, source, destination, NULL, dirty); (void)window;"
  ],
  [
    "Present dirty lost",
    "core_swap_chain8_present",
    "chain->backend, source, destination, window, dirty);",
    "chain->backend, source, destination, window, NULL); (void)dirty;"
  ],
  [
    "Present status lost",
    "core_swap_chain8_present",
    "return chain->methods->present(chain->backend, source, destination, window, dirty);",
    "chain->methods->present(chain->backend, source, destination, window, dirty); return D3D_OK;"
  ],
  [
    "GetBack nullout success",
    "core_swap_chain8_get_back_buffer",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) return D3D_OK;"
  ],
  [
    "GetBack clears error output",
    "core_swap_chain8_get_back_buffer",
    "    D8CoreSwapChain *chain = (D8CoreSwapChain *)iface;",
    "    *out = NULL; D8CoreSwapChain *chain = (D8CoreSwapChain *)iface;"
  ],
  [
    "GetBack index lost",
    "core_swap_chain8_get_back_buffer",
    "chain->backend, index, (UINT)type, &surface);",
    "chain->backend, 0, (UINT)type, &surface); (void)index;"
  ],
  [
    "GetBack type lost",
    "core_swap_chain8_get_back_buffer",
    "chain->backend, index, (UINT)type, &surface);",
    "chain->backend, index, 0, &surface); (void)type;"
  ],
  [
    "GetBack status lost",
    "core_swap_chain8_get_back_buffer",
    "if (FAILED(hr)) return hr;",
    "if (FAILED(hr)) return D3D_OK;"
  ],
  [
    "GetBack count coupled",
    "core_swap_chain8_get_back_buffer",
    "(IUnknown *)chain->device, FALSE, out);",
    "(IUnknown *)chain->device, TRUE, out);"
  ],
  [
    "GetBack container chain",
    "core_swap_chain8_get_back_buffer",
    "(IUnknown *)chain->device, FALSE, out);",
    "(IUnknown *)iface, FALSE, out);"
  ],
  [
    "Wrap stale",
    "d8wg_core_wrap_swap_chain8",
    "    if (out) *out = NULL;",
    "    /* stale */"
  ],
  [
    "Wrap input not consumed",
    "d8wg_core_wrap_swap_chain8",
    "    if (owned) IUnknown_Release((IUnknown *)owned);",
    "    (void)owned;"
  ],
  [
    "Wrap OOM wrong status",
    "d8wg_core_wrap_swap_chain8",
    "if (!view) { hr = E_OUTOFMEMORY; goto done; }",
    "if (!view) { hr = D3DERR_INVALIDCALL; goto done; }"
  ],
  [
    "Wrap candidate leaked",
    "d8wg_core_wrap_swap_chain8",
    "            HeapFree(GetProcessHeap(), 0, view);",
    "            /* leaked */"
  ],
  [
    "Wrap race no winner",
    "d8wg_core_wrap_swap_chain8",
    "if (!cached) { hr = D3DERR_INVALIDCALL; goto done; }",
    "if (!cached) { hr = D3D_OK; goto done; }"
  ],
  [
    "Wrap wrong device",
    "d8wg_core_wrap_swap_chain8",
    "view->device != device",
    "FALSE"
  ],
  [
    "Wrap wrong class",
    "d8wg_core_wrap_swap_chain8",
    "view->iface.lpVtbl != &g_core_swap_chain8_vtbl",
    "FALSE"
  ],
  [
    "Hotcache allocation",
    "d8wg_core_wrap_swap_chain8",
    "\n    view = (D8CoreSwapChain *)cached;",
    "\n    view = NULL;"
  ],
  [
    "Borrowed wrong class",
    "d8wg_core_swap_chain_backend8",
    "texture->lpVtbl != &g_core_swap_chain8_vtbl",
    "FALSE"
  ],
  [
    "Borrowed missing backend",
    "d8wg_core_swap_chain_backend8",
    "return ((D8CoreSwapChain *)texture)->backend;",
    "return NULL;"
  ]
];
const common=fs.readFileSync(root+'/d3d8proxy/d3d8_core_view.h','utf8');
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to,kind]of plan){const input=kind?common:source;const[a,b]=span(input,fn),body=input.slice(a,b);assert(body.includes(from),name);const candidate=input.slice(0,a)+body.replace(from,to)+input.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_swap_chain_adapter_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,...(kind?{D8WG_CORE_VIEW_SOURCE:file}:{D8WG_SWAP_CHAIN_ADAPTER_SOURCE:file})}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Surface8 adapter line \d+:|Assertion failed: \([^\n]*\), function \w+, file adapter\.c, line \d+\.|adapter\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS SDK8 SwapChain8 adapter mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
