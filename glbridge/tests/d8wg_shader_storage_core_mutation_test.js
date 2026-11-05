"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'view8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
  [
    "ordinary9 admitted",
    "shader_storage_reference8",
    "if (device_compatibility_api(device) != 8u)",
    "if (FALSE)"
  ],
  [
    "declaration foreign owner admitted",
    "shader_storage_reference8",
    "decl->device != device",
    "FALSE"
  ],
  [
    "shader foreign owner admitted",
    "shader_storage_reference8",
    "shader->device != device",
    "FALSE"
  ],
  [
    "declaration hold omitted",
    "shader_storage_reference8",
    "if (retain) decl_binding_add_ref(decl);",
    "if (retain) (void)decl;"
  ],
  [
    "shader hold omitted",
    "shader_storage_reference8",
    "if (retain) shader_binding_add_ref(shader);",
    "if (retain) (void)shader;"
  ],
  [
    "declaration release omitted",
    "shader_storage_reference8",
    "else decl_binding_release(decl);",
    "else (void)decl;"
  ],
  [
    "shader release omitted",
    "shader_storage_reference8",
    "else shader_binding_release(shader);",
    "else (void)shader;"
  ],
  [
    "pixel shader unsupported",
    "shader_storage_reference8",
    "|| vtbl == &g_pixel_shader_vtbl",
    "|| FALSE"
  ],
  [
    "public-zero declaration clone omitted",
    "shader_storage_reference8",
    "if (retain) decl_binding_add_ref(decl);",
    "if (retain) { if (decl->refcount) decl_binding_add_ref(decl); }"
  ],
  [
    "public-zero shader clone omitted",
    "shader_storage_reference8",
    "if (retain) shader_binding_add_ref(shader);",
    "if (retain) { if (shader->refcount) shader_binding_add_ref(shader); }"
  ],
  [
    "declaration public pin used",
    "shader_storage_reference8",
    "if (retain) decl_binding_add_ref(decl);",
    "if (retain) decl_add_ref(&decl->iface);"
  ],
  [
    "shader public pin used",
    "shader_storage_reference8",
    "if (retain) shader_binding_add_ref(shader);",
    "if (retain) shader_public_add_ref(shader);"
  ],
  [
    "declaration public release used",
    "shader_storage_reference8",
    "else decl_binding_release(decl);",
    "else decl_release(&decl->iface);"
  ],
  [
    "shader public release used",
    "shader_storage_reference8",
    "else shader_binding_release(shader);",
    "else shader_public_release(shader);"
  ],
  [
    "extra device pin",
    "shader_storage_reference8",
    "if (retain) decl_binding_add_ref(decl);",
    "if (retain) { device_add_ref(iface); decl_binding_add_ref(decl); }"
  ],
  [
    "add wrapper releases",
    "d9wg_d3d9_core_shader_storage_add_ref8",
    "shader_storage_reference8(device, object, TRUE)",
    "shader_storage_reference8(device, object, FALSE)"
  ],
  [
    "release wrapper adds",
    "d9wg_d3d9_core_shader_storage_release8",
    "shader_storage_reference8(device, object, FALSE)",
    "shader_storage_reference8(device, object, TRUE)"
  ]
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_shader_storage_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_SHADER_STORAGE_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target binding line \d+:|Assertion failed: \([^\n]*\), function \w+, file target\.c, line \d+\.|target\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 shader storage-hold mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
