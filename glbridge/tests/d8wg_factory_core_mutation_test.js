"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),sources={eight:fs.readFileSync(root+"/d3d8proxy/d3d8_proxy.c","utf8"),nine:fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8")},dir=fs.mkdtempSync(path.join(os.tmpdir(),"d8wg-factory-core-mut-"));
function mutate(source,name,before,after){const m=new RegExp("^[^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(source);assert(m,name);let end=source.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}const body=source.slice(m.index,end);assert(body.includes(before),name+" "+before);return source.slice(0,m.index)+body.replace(before,after)+source.slice(end);}
const cases=[
    ["missing backend factory","eight","Direct3DCreate8","d3d->backend = d9wg_d3d9_core_create8();","d3d->backend = NULL;"],
    ["duplicated backend factory","eight","Direct3DCreate8","d3d->backend = d9wg_d3d9_core_create8();","d3d->backend = d9wg_d3d9_core_create8();d3d->backend = d9wg_d3d9_core_create8();"],
    ["backend factory leaked at final wrapper release","eight","d3d_release","d9wg_d3d9_core_root_release(d3d->backend);","(void)d3d->backend;"],
    ["public wrapper AddRef skipped","eight","d3d_add_ref","InterlockedIncrement(&d3d_from_iface(iface)->refcount)","d3d_from_iface(iface)->refcount"],
    ["adapter count bypasses backend","eight","d3d_get_adapter_count","return d9wg_d3d9_core_adapter_count(d3d_from_iface(iface)->backend);","(void)iface;return 1;"],
    ["adapter monitor loses adapter index","eight","d3d_get_adapter_monitor","return d9wg_d3d9_core_adapter_monitor(d3d_from_iface(iface)->backend, adapter);","(void)adapter;return d9wg_d3d9_core_adapter_monitor(d3d_from_iface(iface)->backend, 0);"],
    ["private COM identity exposed as D3D8","eight","Direct3DCreate8","return &d3d->iface;","return (IDirect3D8*)d3d->backend;"],
    ["wrapper leaked when backend construction fails","eight","Direct3DCreate8","HeapFree(GetProcessHeap(), 0, d3d);","(void)d3d;"],
    ["SDK120 rejected","eight","Direct3DCreate8","sdk_version != D3D_SDK_VERSION_DX80","TRUE"],
    ["D3D8 SDK debug bit incorrectly accepted","eight","Direct3DCreate8","D8Direct3D *d3d;","D8Direct3D *d3d;sdk_version &= 0x7fffffffu;"],
    ["backend creates a D3D9 Hello in D3D8 stream","nine","d9wg_d3d9_core_create8","d3d_create_root(D3D_SDK_VERSION_9C, FALSE, 8)","d3d_create_root(D3D_SDK_VERSION_9C, TRUE, 8)"],
    ["D3D8 token passed to D3D9 backend constructor","nine","d9wg_d3d9_core_create8","d3d_create_root(D3D_SDK_VERSION_9C, FALSE, 8)","d3d_create_root(220u, FALSE, 8)"],
];
try{for(const[label,kind,name,before,after]of cases){const file=dir+"/proxy.c";fs.writeFileSync(file,mutate(sources[kind],name,before,after));const env={...process.env,[kind==="eight"?"D8WG_FACTORY_CORE_SOURCE":"D8WG_FACTORY_CORE_BACKEND_SOURCE"]:file},r=spawnSync(process.execPath,[__dirname+"/d8wg_factory_core_test.js"],{env,encoding:"utf8",timeout:30000}),out=r.stdout+r.stderr;assert(!r.error,label);assert.notEqual(r.status,0,"survived "+label);assert(!/error:|undefined reference|Undefined symbols/.test(out),label+" compile failure: "+out);assert.match(out,/D3D8 factory core line|Assertion.*failed|Assertion failed/,label+" must fail a runtime assertion: "+out);console.log("KILL "+label);}console.log("PASS D3D8 factory/core mutations:12/12, strict compilation then runtime assertions");}finally{fs.rmSync(dir,{recursive:true,force:true});}
