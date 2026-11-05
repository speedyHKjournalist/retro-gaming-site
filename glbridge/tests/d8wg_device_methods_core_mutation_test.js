"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'view8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
  [
    "ordinary9 table admitted",
    "d9wg_d3d9_core_device_methods8",
    "device_compatibility_api(device_from_iface(iface)) != 8u",
    "FALSE"
  ],
  [
    "foreign table admitted",
    "d9wg_d3d9_core_device_methods8",
    "iface->lpVtbl != &g_device_vtbl",
    "FALSE"
  ],
  [
    "wrong8 memory budget",
    "core_device_get_available_texture_mem8",
    "128u * 1024u",
    "256u * 1024u"
  ],
  [
    "legacy8 pure read refused",
    "device_is_pure",
    "device_compatibility_api(device) != 8u",
    "TRUE"
  ],
  [
    "ordinary9 pure read permitted",
    "device_is_pure",
    "device_compatibility_api(device) != 8u",
    "FALSE"
  ],
  [
    "private comparison truncated",
    "constant_vector_equal",
    "i < 16u",
    "i < 15u"
  ],
  [
    "storage copy omitted",
    "set_constant_f",
    "        CopyMemory(target, source, 16u);",
    "        (void)target;"
  ],
  [
    "storage copy truncated",
    "set_constant_f",
    "        CopyMemory(target, source, 16u);",
    "        CopyMemory(target, source, 15u);"
  ],
  [
    "repeat writes always emitted",
    "set_constant_f",
    "if (constant_vector_equal(target, source))",
    "if (FALSE && constant_vector_equal(target, source))"
  ],
  [
    "wrong constant storage bank",
    "set_constant_f",
    "is_pixel ? device->ps_const_f : device->vs_const_f",
    "is_pixel ? device->vs_const_f : device->ps_const_f"
  ],
  [
    "recorded floats mutate live",
    "set_constant_f",
    "if (state_block_record_constant(",
    "if (FALSE && state_block_record_constant("
  ],
  [
    "wrong dirty starting register",
    "set_constant_f",
    "start + first_dirty, last_dirty - first_dirty + 1",
    "start + first_dirty + 1u, last_dirty - first_dirty + 1"
  ],
  [
    "VS set empty call refused",
    "core_device_set_vertex_shader_constant8",
    "    if (!count) return D3D_OK;",
    "    if (!count) return D3DERR_INVALIDCALL;"
  ],
  [
    "VS set accepts9 register limit",
    "core_device_set_vertex_shader_constant8",
    "> 96u",
    "> 256u"
  ],
  [
    "VS get empty call refused",
    "core_device_get_vertex_shader_constant8",
    "    if (!count) return D3D_OK;",
    "    if (!count) return D3DERR_INVALIDCALL;"
  ],
  [
    "VS get accepts9 register limit",
    "core_device_get_vertex_shader_constant8",
    "> 96u",
    "> 256u"
  ],
  [
    "VS setter targets other bank",
    "core_device_set_vertex_shader_constant8",
    "device_set_vertex_shader_constant_f(",
    "device_set_pixel_shader_constant_f("
  ],
  [
    "VS getter targets other bank",
    "core_device_get_vertex_shader_constant8",
    "device_get_vertex_shader_constant_f(",
    "device_get_pixel_shader_constant_f("
  ],
  [
    "PS set empty call refused",
    "core_device_set_pixel_shader_constant8",
    "    if (!count) return D3D_OK;",
    "    if (!count) return D3DERR_INVALIDCALL;"
  ],
  [
    "PS set accepts9 register limit",
    "core_device_set_pixel_shader_constant8",
    "> 8u",
    "> 224u"
  ],
  [
    "PS get empty call refused",
    "core_device_get_pixel_shader_constant8",
    "    if (!count) return D3D_OK;",
    "    if (!count) return D3DERR_INVALIDCALL;"
  ],
  [
    "PS get accepts9 register limit",
    "core_device_get_pixel_shader_constant8",
    "> 8u",
    "> 224u"
  ],
  [
    "PS setter targets other bank",
    "core_device_set_pixel_shader_constant8",
    "device_set_pixel_shader_constant_f(",
    "device_set_vertex_shader_constant_f("
  ],
  [
    "PS getter targets other bank",
    "core_device_get_pixel_shader_constant8",
    "device_get_pixel_shader_constant_f(",
    "device_get_vertex_shader_constant_f("
  ],
  [
    "float equality loses signedzero andNaN duplicates",
    "constant_vector_equal",
    "static BOOL constant_vector_equal(const float *left, const float *right)\n{\n    const unsigned char *a = (const unsigned char *)left;\n    const unsigned char *b = (const unsigned char *)right;\n    for (UINT i = 0; i < 16u; ++i) if (a[i] != b[i]) return FALSE;\n    return TRUE;\n}",
    "static BOOL constant_vector_equal(const float *left, const float *right)\n{\n    return left[0]==right[0] && left[1]==right[1] && left[2]==right[2] && left[3]==right[3];\n}"
  ]
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_device_methods_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_DEVICE_METHODS_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Device methods line \d+:|Assertion failed: \([^\n]*\), function \w+, file methods\.c, line \d+\.|methods\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 device method-policy mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
