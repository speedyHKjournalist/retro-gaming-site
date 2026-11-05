"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'view8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const releaseStart=source.indexOf('static ULONG WINAPI device_release('), parentStart=source.indexOf('        /* The implicit surfaces outlive',releaseStart), parentEnd=source.indexOf('        HeapFree(GetProcessHeap(), 0, device);',parentStart),parentTail=source.slice(parentStart,parentEnd);
const plan=[
  ["parent freed before metadata callbacks", "device_release", parentTail, "        IDirect3D9_Release(&device->parent->iface);\n"+parentTail.replace("        /* Registry queries in retirement callbacks still inspect the parent\n         * compatibility profile. Keep that factory alive through every view. */\n        IDirect3D9_Release(&device->parent->iface);\n", "")],
  [
    "device classifier omitted",
    "core_view_device8",
    "if (vtbl == &g_device_vtbl) device = (D9Device *)object;",
    "if (FALSE) device = (D9Device *)object;"
  ],
  [
    "ordinary9 device admitted",
    "core_view_device8",
    "device && device_compatibility_api(device) == 8u",
    "device && (device_compatibility_api(device) == 8u || vtbl == &g_device_vtbl)"
  ],
  [
    "device retire omitted",
    "device_release",
    "        core_view_retire8(device);",
    "        (void)device;"
  ],
  [
    "device retired before both surfaces",
    "device_release",
    "        if (device->implicit_back_buffer) {\n            core_view_retire8(device->implicit_back_buffer);",
    "        core_view_retire8(device);\n        if (device->implicit_back_buffer) {\n            core_view_retire8(device->implicit_back_buffer);"
  ],
  [
    "device retired before depth",
    "device_release",
    "        if (device->implicit_depth_stencil) {\n            core_view_retire8(device->implicit_depth_stencil);",
    "        core_view_retire8(device);\n        if (device->implicit_depth_stencil) {\n            core_view_retire8(device->implicit_depth_stencil);"
  ],
  [
    "device retired before private finalizers",
    "device_release",
    "        InterlockedIncrement(&device->refcount);",
    "        InterlockedIncrement(&device->refcount);\n        core_view_retire8(device);"
  ],
  [
    "revived device metadata retired",
    "device_release",
    "        device->releasing_owned_refs = FALSE;",
    "        device->releasing_owned_refs = FALSE;\n        if (refs) core_view_retire8(device);"
  ],
  [
    "attach adds device pin",
    "d9wg_d3d9_core_attach_view8",
    "    entry->object = object;",
    "    entry->object = object;\n    if (object->lpVtbl == (const void *)&g_device_vtbl) IDirect3DDevice9_AddRef((IDirect3DDevice9 *)object);"
  ],
  [
    "lookup adds device pin",
    "d9wg_d3d9_core_get_view8",
    "    EnterCriticalSection(&g_transport_lock);",
    "    if (object->lpVtbl == (const void *)&g_device_vtbl) IDirect3DDevice9_AddRef((IDirect3DDevice9 *)object);\n    EnterCriticalSection(&g_transport_lock);"
  ]
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_device_view_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_DEVICE_VIEW_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target binding line \d+:|Assertion failed: \([^\n]*\), function \w+, file target\.c, line \d+\.|target\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 weak device-view mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
