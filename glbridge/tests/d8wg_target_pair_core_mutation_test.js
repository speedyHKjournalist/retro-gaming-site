"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'target8-pair-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
  [
    "ordinary9 admitted",
    "d9wg_d3d9_core_set_render_target8",
    "device_compatibility_api(device_from_iface(iface)) != 8u",
    "device_compatibility_api(device_from_iface(iface)) == 999u"
  ],
  [
    "foreign device admitted",
    "d9wg_d3d9_core_set_render_target8",
    "iface->lpVtbl != &g_device_vtbl",
    "FALSE"
  ],
  [
    "foreign color admitted",
    "d9wg_d3d9_core_set_render_target8",
    "color->device != device",
    "FALSE"
  ],
  [
    "foreign depth admitted",
    "d9wg_d3d9_core_set_render_target8",
    "depth->device != device",
    "FALSE"
  ],
  [
    "invalid color pool",
    "d9wg_d3d9_core_set_render_target8",
    "desc.Pool != D3DPOOL_DEFAULT",
    "FALSE"
  ],
  [
    "invalid color usage",
    "d9wg_d3d9_core_set_render_target8",
    "!(desc.Usage & D3DUSAGE_RENDERTARGET)",
    "FALSE"
  ],
  [
    "invalid depth usage",
    "d9wg_d3d9_core_set_render_target8",
    "!(desc.Usage & D3DUSAGE_DEPTHSTENCIL)",
    "FALSE"
  ],
  [
    "short depth width",
    "d9wg_d3d9_core_set_render_target8",
    "desc.Width < width",
    "FALSE"
  ],
  [
    "short depth height",
    "d9wg_d3d9_core_set_render_target8",
    "desc.Height < height",
    "FALSE"
  ],
  [
    "different samples",
    "d9wg_d3d9_core_set_render_target8",
    "desc.MultiSampleType != samples",
    "samples == 0xffffffffu"
  ],
  [
    "cube accepted as depth",
    "d9wg_d3d9_core_set_render_target8",
    "|| depth->cube_texture",
    "|| FALSE"
  ],
  [
    "incorrect implicit depth handle",
    "d9wg_d3d9_core_set_render_target8",
    "depth_command.depth_texture_handle = D9WG_AUTO_DEPTH_STENCIL_HANDLE",
    "depth_command.depth_texture_handle = 0"
  ],
  [
    "depth level lost",
    "d9wg_d3d9_core_set_render_target8",
    "        depth_command.width = desc.Width;",
    "        depth_command.depth_level = 0;\n        depth_command.width = desc.Width;"
  ],
  [
    "color state not replaced",
    "d9wg_d3d9_core_set_render_target8",
    "if (color) device_replace_render_target_surface(device, 0, color);",
    "if (FALSE && color) device_replace_render_target_surface(device, 0, color);"
  ],
  [
    "depth commits after first emit",
    "d9wg_d3d9_core_set_render_target8",
    "    device_replace_depth_stencil_surface(device, depth);\n",
    "    if (FALSE) device_replace_depth_stencil_surface(device, depth);\n"
  ],
  [
    "depth emission omitted",
    "d9wg_d3d9_core_set_render_target8",
    "if (!emit_command(D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL,",
    "if (FALSE && !emit_command(D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL,"
  ],
  [
    "viewport update omitted",
    "d9wg_d3d9_core_set_render_target8",
    "return device_set_live_viewport(device, &viewport, TRUE);",
    "(void)viewport; (void)device_set_live_viewport; return D3D_OK;"
  ],
  [
    "equal viewport emits",
    "d9wg_d3d9_core_set_render_target8",
    "&viewport, TRUE",
    "&viewport, FALSE"
  ],
  [
    "NULLcolor changes viewport",
    "d9wg_d3d9_core_set_render_target8",
    "if (!color) return D3D_OK;",
    "if (FALSE && !color) return D3D_OK;"
  ],
  [
    "bound depth marked unbound",
    "device_replace_depth_stencil_surface",
    "device->depth_stencil_unbound = surface ? FALSE : TRUE;",
    "device->depth_stencil_unbound = TRUE;"
  ],
  [
    "same color released before retain",
    "device_replace_render_target_surface",
    "if (surface)\n        surface_binding_add_ref(surface);\n    device->render_target_surfaces[index] = surface;\n    if (old_surface)\n        surface_binding_release(old_surface);",
    "if (old_surface) surface_binding_release(old_surface);\n    if (surface) surface_binding_add_ref(surface);\n    device->render_target_surfaces[index] = surface;"
  ],
  [
    "texture child returns device",
    "d9wg_d3d9_core_surface_owner8",
    "surface->texture_child && surface->texture",
    "FALSE && surface->texture_child && surface->texture"
  ],
  [
    "standalone returns storage texture",
    "d9wg_d3d9_core_surface_owner8",
    "surface->texture_child && surface->texture",
    "surface->texture"
  ],
  [
    "cube reports wrong kind",
    "d9wg_d3d9_core_surface_owner8",
    "*kind = 4;",
    "*kind = 3;"
  ],
  [
    "owner invalid leaves stale output",
    "d9wg_d3d9_core_surface_owner8",
    "*owner = NULL;",
    "(void)owner;"
  ]
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_target_pair_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_TARGET_PAIR_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target pair line \d+:|Assertion failed: \([^\n]*\), function \w+, file pair\.c, line \d+\.|pair\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}const adapter=fs.readFileSync(root+'/d3d8proxy/d3d8_core_surface.c','utf8'),selectorPlan=[["surface selector returns NULL", "? ((D8CoreSurface *)iface)->backend : NULL", "? NULL : NULL"], ["surface selector accepts foreign class", "iface->lpVtbl == &g_core_surface8_vtbl", "TRUE"], ["surface selector accepts NULL", "    return iface && iface->lpVtbl", "    if (!iface) return (struct IDirect3DSurface9 *)1;\n    return iface && iface->lpVtbl"], ["surface selector changes references", "    return iface && iface->lpVtbl", "    if (iface && iface->lpVtbl == &g_core_surface8_vtbl) IUnknown_AddRef((IUnknown *)((D8CoreSurface *)iface)->backend);\n    return iface && iface->lpVtbl"]];let selectorKilled=0;for(const[name,from,to]of selectorPlan){const[a,b]=span(adapter,'d8wg_core_surface_backend8'),body=adapter.slice(a,b);assert(body.includes(from),name);const file=dir+'/surface.c';fs.writeFileSync(file,adapter.slice(0,a)+body.replace(from,to)+adapter.slice(b));const r=spawnSync(process.execPath,[__dirname+'/d8wg_surface_adapter_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_SURFACE_ADAPTER_SOURCE:file}});assert(!r.error,name);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Surface8 adapter line \d+:/.test(out),name+' requires actual C check: '+out);selectorKilled++;}console.log('PASS surface8 borrowed backend selector mutations: '+selectorKilled+'/4 strictly compiled then failed actual C assertions');console.log('PASS core8 target pair/owner mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
