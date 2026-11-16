"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'view8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
  [
    "cleanup callback omitted",
    "core_device_cleanup8",
    "if (cleanup) cleanup(view);",
    "(void)cleanup; (void)view;"
  ],
  [
    "cleanup callback under lock",
    "core_device_cleanup8",
    "    LeaveCriticalSection(&g_transport_lock);\n    if (cleanup) cleanup(view);",
    "    if (cleanup) cleanup(view);\n    LeaveCriticalSection(&g_transport_lock);"
  ],
  [
    "retiring cleanup not ignored",
    "core_device_cleanup8",
    "entry && !entry->retiring",
    "entry"
  ],
  [
    "cleanup callback field omitted",
    "core_view_attach8",
    "    entry->cleanup = cleanup;",
    "    (void)cleanup;"
  ],
  [
    "typed attach permits NULL cleanup",
    "d9wg_d3d9_core_attach_device_view8",
    "|| !cleanup",
    "|| FALSE"
  ],
  [
    "typed attach adds public pin",
    "d9wg_d3d9_core_attach_device_view8",
    "    return core_view_attach8(",
    "    IDirect3DDevice9_AddRef(device);\n    return core_view_attach8("
  ],
  [
    "ordinary9 explicit cleanup accepted",
    "d9wg_d3d9_core_release_owned8",
    "device_compatibility_api(device) != 8u",
    "FALSE"
  ],
  [
    "nested explicit cleanup accepted",
    "d9wg_d3d9_core_release_owned8",
    "|| device->releasing_owned_refs",
    "|| FALSE"
  ],
  [
    "temporary pin omitted",
    "d9wg_d3d9_core_release_owned8",
    "    IDirect3DDevice9_AddRef(iface);",
    "    (void)iface;"
  ],
  [
    "temporary pin leaked",
    "d9wg_d3d9_core_release_owned8",
    "    IDirect3DDevice9_Release(iface);",
    "    (void)iface;"
  ],
  [
    "owned guard omitted",
    "d9wg_d3d9_core_release_owned8",
    "    device->releasing_owned_refs = TRUE;",
    "    device->releasing_owned_refs = FALSE;"
  ],
  [
    "owned guard retained",
    "d9wg_d3d9_core_release_owned8",
    "    device->releasing_owned_refs = FALSE;",
    "    device->releasing_owned_refs = TRUE;"
  ],
  [
    "shared owned cleanup omitted",
    "d9wg_d3d9_core_release_owned8",
    "    device_release_owned_references(device);",
    "    (void)device;"
  ],
  [
    "guard restored before cleanup",
    "d9wg_d3d9_core_release_owned8",
    "    device_release_owned_references(device);\n    device->releasing_owned_refs = FALSE;",
    "    device->releasing_owned_refs = FALSE;\n    device_release_owned_references(device);"
  ],
  [
    "owned cleanup hook omitted",
    "device_release_owned_references",
    "    core_device_cleanup8(device);",
    "    (void)device;"
  ],
  [
    "notification after recording",
    "device_release_owned_references",
    "static void device_release_owned_references(D9Device *device)\n{\n    TRACE(\"ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n    core_device_cleanup8(device);\n    device_cancel_recording(device);\n    device_clear_state_block_handles8(device);\n    device_clear_bindings(device);\n    device_clear_fvf_cache(device);\n    TRACE(\"LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n}",
    "static void device_release_owned_references(D9Device *device)\n{\n    TRACE(\"ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n    device_cancel_recording(device);\n    core_device_cleanup8(device);\n    device_clear_state_block_handles8(device);\n    device_clear_bindings(device);\n    device_clear_fvf_cache(device);\n    TRACE(\"LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n}"
  ],
  [
    "notification after blocks",
    "device_release_owned_references",
    "static void device_release_owned_references(D9Device *device)\n{\n    TRACE(\"ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n    core_device_cleanup8(device);\n    device_cancel_recording(device);\n    device_clear_state_block_handles8(device);\n    device_clear_bindings(device);\n    device_clear_fvf_cache(device);\n    TRACE(\"LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n}",
    "static void device_release_owned_references(D9Device *device)\n{\n    TRACE(\"ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n    device_cancel_recording(device);\n    device_clear_state_block_handles8(device);\n    core_device_cleanup8(device);\n    device_clear_bindings(device);\n    device_clear_fvf_cache(device);\n    TRACE(\"LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n}"
  ],
  [
    "notification after bindings",
    "device_release_owned_references",
    "static void device_release_owned_references(D9Device *device)\n{\n    TRACE(\"ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n    core_device_cleanup8(device);\n    device_cancel_recording(device);\n    device_clear_state_block_handles8(device);\n    device_clear_bindings(device);\n    device_clear_fvf_cache(device);\n    TRACE(\"LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n}",
    "static void device_release_owned_references(D9Device *device)\n{\n    TRACE(\"ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n    device_cancel_recording(device);\n    device_clear_state_block_handles8(device);\n    device_clear_bindings(device);\n    core_device_cleanup8(device);\n    device_clear_fvf_cache(device);\n    TRACE(\"LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n}"
  ],
  [
    "notification after FVFcache",
    "device_release_owned_references",
    "static void device_release_owned_references(D9Device *device)\n{\n    TRACE(\"ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n    core_device_cleanup8(device);\n    device_cancel_recording(device);\n    device_clear_state_block_handles8(device);\n    device_clear_bindings(device);\n    device_clear_fvf_cache(device);\n    TRACE(\"LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n}",
    "static void device_release_owned_references(D9Device *device)\n{\n    TRACE(\"ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n    device_cancel_recording(device);\n    device_clear_state_block_handles8(device);\n    device_clear_bindings(device);\n    device_clear_fvf_cache(device);\n    core_device_cleanup8(device);\n    TRACE(\"LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld\",\n            device->handle,\n            InterlockedCompareExchange(&device->refcount, 0, 0),\n            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));\n}"
  ]
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_device_cleanup_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_DEVICE_CLEANUP_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target binding line \d+:|Assertion failed: \([^\n]*\), function \w+, file target\.c, line \d+\.|target\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 device cleanup-observer mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
