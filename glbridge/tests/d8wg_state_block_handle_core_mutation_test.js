"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'token8-core-mut-')),source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
const plan=[
  [
    "null output reports success",
    "d9wg_d3d9_core_create_state_block_handle8",
    "if (!out) return D3DERR_INVALIDCALL;",
    "if (!out) return D3D_OK;"
  ],
  [
    "stale output",
    "d9wg_d3d9_core_create_state_block_handle8",
    "    *out = 0;",
    "    /* stale */"
  ],
  [
    "wrong profile",
    "d9wg_d3d9_core_create_state_block_handle8",
    "!= 8u",
    "!= 9u"
  ],
  [
    "null recorder guard",
    "d9wg_d3d9_core_create_state_block_handle8",
    "!iface || !recorder",
    "!iface || recorder"
  ],
  [
    "create missing device guard",
    "d9wg_d3d9_core_create_state_block_handle8",
    "    IDirect3DDevice9_AddRef(iface);",
    "    /* missing guard */"
  ],
  [
    "success output lost",
    "d9wg_d3d9_core_create_state_block_handle8",
    "        *out = state_block_publish_handle8(state_block_from_iface(created));",
    "        { *out = 0; (void)&state_block_publish_handle8; }"
  ],
  [
    "token allocator collision",
    "state_block_publish_handle8",
    "while (state_block_handle8(block->device, token));",
    "while (0);"
  ],
  [
    "token not published",
    "state_block_adopt_handle8",
    "    block->handle8 = token;",
    "    block->handle8 = token ^ 1u;"
  ],
  [
    "parent cycle",
    "state_block_adopt_handle8",
    "    block->device_parent_ref = FALSE;",
    "    block->device_parent_ref = TRUE;"
  ],
  [
    "parent pin retained",
    "state_block_adopt_handle8",
    "    device_child_release(block->device);",
    "    /* retained pin */"
  ],
  [
    "lookup ignores profile",
    "state_block_handle8",
    "device_compatibility_api(device) != 8u",
    "device_compatibility_api(device) == 8u"
  ],
  [
    "lookup wrong key",
    "state_block_handle8",
    "block->handle8 != token",
    "block->handle8 == token"
  ],
  [
    "lookup allowed during teardown",
    "state_block_handle8",
    "|| device->releasing_owned_refs",
    "|| FALSE"
  ],
  [
    "apply ignores snapshot",
    "d9wg_d3d9_core_apply_state_block_handle8",
    "HRESULT hr = IDirect3DStateBlock9_Apply(&block->iface);",
    "HRESULT hr = D3D_OK;"
  ],
  [
    "apply no storage pin",
    "d9wg_d3d9_core_apply_state_block_handle8",
    "    IDirect3DStateBlock9_AddRef(&block->iface);",
    "    /* missing operation pin */"
  ],
  [
    "capture no storage pin",
    "d9wg_d3d9_core_capture_state_block_handle8",
    "    IDirect3DStateBlock9_AddRef(&block->iface);",
    "    /* missing operation pin */"
  ],
  [
    "capture device guard leaked",
    "d9wg_d3d9_core_capture_state_block_handle8",
    "    IDirect3DDevice9_Release(iface);",
    "    /* device guard leaked */"
  ],
  [
    "capture old ownership leaked",
    "d9wg_d3d9_core_capture_state_block_handle8",
    "            IDirect3DStateBlock9_Release(&block->iface); /* token ownership */",
    "            /* token ownership leaked */"
  ],
  [
    "capture token changes",
    "d9wg_d3d9_core_capture_state_block_handle8",
    "state_block_adopt_handle8(state_block_from_iface(created), token);",
    "state_block_adopt_handle8(state_block_from_iface(created), token + 1);"
  ],
  [
    "capture operation pin leaked",
    "d9wg_d3d9_core_capture_state_block_handle8",
    "    IDirect3DStateBlock9_Release(&block->iface); /* operation storage pin */",
    "    /* operation pin leaked */"
  ],
  [
    "capture replacement guard inverted",
    "d9wg_d3d9_core_capture_state_block_handle8",
    "if (state_block_handle8(device, token) != block)",
    "if (state_block_handle8(device, token) == block)"
  ],
  [
    "capture failure swallowed",
    "d9wg_d3d9_core_capture_state_block_handle8",
    "    return hr;",
    "    return D3D_OK;"
  ],
  [
    "delete reports success on invalid",
    "d9wg_d3d9_core_delete_state_block_handle8",
    "if (!block) return D3DERR_INVALIDCALL;",
    "if (!block) return D3D_OK;"
  ],
  [
    "delete storage leaked",
    "d9wg_d3d9_core_delete_state_block_handle8",
    "    IDirect3DStateBlock9_Release(&block->iface);",
    "    /* leaked block */"
  ],
  [
    "device owned tokens leaked",
    "device_release_owned_references",
    "    device_clear_state_block_handles8(device);",
    "    (void)&device_clear_state_block_handles8;"
  ]
];
function span(s,n){const m=new RegExp('^[A-Za-z_][^;{}\\n]*?\\b'+n+'\\([^;]*?\\)\\s*\\{','m').exec(s);assert(m,n);let e=s.indexOf('{',m.index)+1,d=1;for(;d;e++){if(s[e]==='{')d++;if(s[e]==='}')d--;}return[m.index,e];}
try{let killed=0;for(const[name,fn,from,to]of plan){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(from),name);const candidate=source.slice(0,a)+body.replace(from,to)+source.slice(b),file=dir+'/candidate.c';fs.writeFileSync(file,candidate);const r=spawnSync(process.execPath,[__dirname+'/d8wg_state_block_handle_core_test.js'],{encoding:'utf8',timeout:60000,env:{...process.env,D8WG_STATE_BLOCK_HANDLE_CORE_SOURCE:file}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const out=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(out),name+' compiler failure: '+out);assert(/Target binding line \d+:|Assertion failed: \([^\n]*\), function \w+, file target\.c, line \d+\.|target\.c:\d+:[^\n]*Assertion[^\n]*failed/.test(out),name+' must fail actual C semantic assertion: '+out);killed++;}console.log('PASS core8 DWORD state-block mutations: '+killed+'/'+plan.length+' strictly compiled then failed actual C assertions');}finally{fs.rmSync(dir,{recursive:true,force:true});}
