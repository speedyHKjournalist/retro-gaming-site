"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'view8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
  [
    "get null output success",
    "d9wg_d3d9_core_get_view8",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) return D3D_OK;"
  ],
  [
    "get stale output",
    "d9wg_d3d9_core_get_view8",
    "    *out = NULL;",
    "    /* stale output */"
  ],
  [
    "get callback tombstone ignored",
    "d9wg_d3d9_core_get_view8",
    "entry && entry->retiring ? D3DERR_INVALIDCALL : D3D_OK",
    "FALSE ? D3DERR_INVALIDCALL : D3D_OK"
  ],
  [
    "get wrong view",
    "d9wg_d3d9_core_get_view8",
    "*out = entry->view;",
    "*out = entry->object;"
  ],
  [
    "attach null view success",
    "core_view_attach8",
    "    if (!view || !retire ||",
    "    if (!view) return D3D_OK;\n    if (!view || !retire ||"
  ],
  [
    "attach null callback success",
    "core_view_attach8",
    "    if (!view || !retire ||",
    "    if (!retire) return D3D_OK;\n    if (!view || !retire ||"
  ],
  [
    "duplicate attachment success",
    "core_view_attach8",
    "        return D3DERR_INVALIDCALL;",
    "        return D3D_OK;"
  ],
  [
    "OOM wrong status",
    "core_view_attach8",
    "return E_OUTOFMEMORY;",
    "return D3DERR_INVALIDCALL;"
  ],
  [
    "object key lost",
    "core_view_attach8",
    "    entry->object = object;",
    "    entry->object = NULL;"
  ],
  [
    "metadata lost",
    "core_view_attach8",
    "    entry->view = view;",
    "    entry->view = NULL;"
  ],
  [
    "view creates device cycle",
    "core_view_attach8",
    "    entry->view = view;",
    "    entry->view = view; IDirect3DDevice9_AddRef(&core_view_device8(object)->iface);"
  ],
  [
    "wrong profile",
    "core_view_device8",
    "device_compatibility_api(device) == 8u",
    "device_compatibility_api(device) == 9u"
  ],
  [
    "vb identity rejected",
    "core_view_device8",
    "device = ((D9VertexBuffer *)object)->device;",
    "device = NULL;"
  ],
  [
    "ib identity rejected",
    "core_view_device8",
    "device = ((D9IndexBuffer *)object)->device;",
    "device = NULL;"
  ],
  [
    "texture identity rejected",
    "core_view_device8",
    "device = ((D9Texture *)object)->device;",
    "device = NULL;"
  ],
  [
    "cube identity rejected",
    "core_view_device8",
    "device = ((D9CubeTexture *)object)->device;",
    "device = NULL;"
  ],
  [
    "volume_texture identity rejected",
    "core_view_device8",
    "device = ((D9VolumeTexture *)object)->device;",
    "device = NULL;"
  ],
  [
    "volume identity rejected",
    "core_view_device8",
    "device = ((D9Volume *)object)->texture->device;",
    "device = NULL;"
  ],
  [
    "surface identity rejected",
    "core_view_device8",
    "device = ((D9Surface *)object)->device;",
    "device = NULL;"
  ],
  [
    "implicit chain wrongly exposed",
    "core_view_device8",
    "if (chain->handle) device = chain->device;",
    "device = chain->device;"
  ],
  [
    "callback skipped",
    "core_view_retire8",
    "    entry->retire(entry->view);",
    "    /* lost callback */"
  ],
  [
    "node leaked",
    "core_view_retire8",
    "    HeapFree(GetProcessHeap(), 0, entry);",
    "    /* lost node */"
  ],
  [
    "registry tail lost",
    "core_view_retire8",
    "if (*link == entry) *link = entry->next;",
    "if (*link == entry) *link = NULL;"
  ],
  [
    "vb_destroy_storage missing retirement",
    "vb_destroy_storage",
    "    core_view_retire8(buffer);",
    "    (void)buffer;"
  ],
  [
    "ib_destroy_storage missing retirement",
    "ib_destroy_storage",
    "    core_view_retire8(buffer);",
    "    (void)buffer;"
  ],
  [
    "texture_destroy_storage missing retirement",
    "texture_destroy_storage",
    "    core_view_retire8(texture);",
    "    (void)texture;"
  ],
  [
    "cube_destroy_storage missing retirement",
    "cube_destroy_storage",
    "    core_view_retire8(texture);",
    "    (void)texture;"
  ],
  [
    "volume_texture_destroy_storage missing retirement",
    "volume_texture_destroy_storage",
    "    core_view_retire8(texture);",
    "    (void)texture;"
  ],
  [
    "surface_destroy_storage missing retirement",
    "surface_destroy_storage",
    "    core_view_retire8(surface);",
    "    (void)surface;"
  ],
  [
    "texture_destroy_storage cached view not retired",
    "texture_destroy_storage",
    "core_view_retire8(texture->levels[level].level_surface);",
    "(void)texture->levels[level].level_surface;"
  ],
  [
    "cube_destroy_storage cached view not retired",
    "cube_destroy_storage",
    "core_view_retire8(texture->levels[index].level_surface);",
    "(void)texture->levels[index].level_surface;"
  ],
  [
    "volume_texture_destroy_storage cached view not retired",
    "volume_texture_destroy_storage",
    "core_view_retire8(texture->levels[level].level_volume);",
    "(void)texture->levels[level].level_volume;"
  ],
  [
    "implicit backbuffer view not retired",
    "device_release",
    "core_view_retire8(device->implicit_back_buffer);",
    "(void)device->implicit_back_buffer;"
  ],
  [
    "implicit depth view not retired",
    "device_release",
    "core_view_retire8(device->implicit_depth_stencil);",
    "(void)device->implicit_depth_stencil;"
  ],
  [
    "metadata retired before private finalizers",
    "texture_destroy_storage",
    "static void texture_destroy_storage(D9Texture *texture)\n{\n    D9Texture** link = &texture->device->texture_resources;\n    D9WGDestroyResource destroy;\n    UINT level;\n    while (*link && *link != texture)\n        link = &(*link)->next_device_resource;\n    if (*link)\n        *link = texture->next_device_resource;\n    d9wg_private_clear(&texture->private_data, &d9wg_private_ops);\n    destroy.resource_handle = texture->handle;\n    destroy.resource_kind = D9WG_RESOURCE_TEXTURE_2D;\n    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));\n    for (level = 0; level < texture->level_count; ++level) {\n        d9wg_private_clear(&texture->levels[level].private_data, &d9wg_private_ops);\n        /* The level's surface sub-object dies with the texture; nothing\n         * can still hold it, because every surface reference is one of the\n         * texture references that just reached zero. */\n        if (texture->levels[level].level_surface) {\n            TRACE_SURFACE_FREED(\n                &texture->levels[level].level_surface->iface);\n            core_view_retire8(texture->levels[level].level_surface);\n            HeapFree(GetProcessHeap(), 0,\n                texture->levels[level].level_surface);\n            texture->levels[level].level_surface = NULL;\n        }\n        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);\n    }\n    HeapFree(GetProcessHeap(), 0, texture->levels);\n    core_view_retire8(texture);\n    HeapFree(GetProcessHeap(), 0, texture);\n}",
    "static void texture_destroy_storage(D9Texture *texture)\n{\n    D9Texture** link = &texture->device->texture_resources;\n    D9WGDestroyResource destroy;\n    UINT level;\n    while (*link && *link != texture)\n        link = &(*link)->next_device_resource;\n    if (*link)\n        *link = texture->next_device_resource;\n    core_view_retire8(texture);\n    d9wg_private_clear(&texture->private_data, &d9wg_private_ops);\n    destroy.resource_handle = texture->handle;\n    destroy.resource_kind = D9WG_RESOURCE_TEXTURE_2D;\n    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));\n    for (level = 0; level < texture->level_count; ++level) {\n        d9wg_private_clear(&texture->levels[level].private_data, &d9wg_private_ops);\n        /* The level's surface sub-object dies with the texture; nothing\n         * can still hold it, because every surface reference is one of the\n         * texture references that just reached zero. */\n        if (texture->levels[level].level_surface) {\n            TRACE_SURFACE_FREED(\n                &texture->levels[level].level_surface->iface);\n            core_view_retire8(texture->levels[level].level_surface);\n            HeapFree(GetProcessHeap(), 0,\n                texture->levels[level].level_surface);\n            texture->levels[level].level_surface = NULL;\n        }\n        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);\n    }\n    HeapFree(GetProcessHeap(), 0, texture->levels);\n    HeapFree(GetProcessHeap(), 0, texture);\n}"
  ]
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_object_view_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_OBJECT_VIEW_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target binding line \d+:|Assertion failed: \([^\n]*\), function \w+, file target\.c, line \d+\.|target\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 weak object-view mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
