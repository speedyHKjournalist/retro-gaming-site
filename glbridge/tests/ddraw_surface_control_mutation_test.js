"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process'),root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-surface-control-mut-'));
const sources={main:fs.readFileSync(root+'/ddrawproxy/ddraw_proxy.c','utf8'),surface:fs.readFileSync(root+'/ddrawproxy/ddraw_surface_control.inc','utf8'),control:fs.readFileSync(root+'/ddrawproxy/ddraw_platform_control.inc','utf8')};
const rows=[
 ['legacy parent not pinned','surface','ddraw_platform_primary_ref','IUnknown_AddRef(*draw);','(void)*draw;'],
 ['native primary not pinned','surface','ddraw_platform_primary_ref','IUnknown_AddRef(primary);','(void)primary;'],
 ['native primary loss ignored','surface','ddraw_platform_poll_loss','hr == DDERR_SURFACELOST && !object->platform_primary_lost','FALSE && hr == DDERR_SURFACELOST && !object->platform_primary_lost'],
 ['primary format stays stale','surface','ddraw_platform_primary_format','*format = mode.ddpfPixelFormat;','(void)format;'],
 ['wrong-mode restore accepted','surface','ddraw_platform_restore_surface','IUnknown_Release(draw);\n        if (FAILED(hr)) return hr;','IUnknown_Release(draw);\n        if (FALSE && FAILED(hr)) return hr;'],
 ['restored surface stays lost','surface','ddraw_platform_restore_surface','surface->cooperative_lost = FALSE;','surface->cooperative_lost = TRUE;'],
 ['flip children stay lost','surface','ddraw_platform_restore_surface','        walk->cooperative_lost = FALSE;','        walk->cooperative_lost = TRUE;'],
 ['aliases stay lost','surface','ddraw_platform_restore_surface','if (walk->storage == surface->storage) walk->cooperative_lost = FALSE;','if (walk->storage == surface->storage) walk->cooperative_lost = TRUE;'],
 ['retired creator publishes a primary','surface','ddraw_platform_primary_attach','object->platform_ddraw == root && object->primary == surface &&\n                object->ref > 0','root != NULL && surface != NULL'],
 ['legacy primary uses extended descriptor','surface','ddraw_platform_primary_attach','hr = IDirectDraw_CreateSurface','hr = IDirectDraw7_CreateSurface'],
 ['mode change ignores other owners','main','ddraw_SetDisplayModeForVersion','ddraw_platform_lose_all_video_surfaces();','ddraw_platform_lose_video_surfaces(object);'],
 ['restore snapshot does not pin','main','ddraw_RestoreAllSurfaces','surface_private_addref(surface);','(void)surface;'],
 ['native parent destroyed before primary','control','ddraw_platform_release','if (primary) IUnknown_Release(primary);\n    if (native) IUnknown_Release(native);','if (native) IUnknown_Release(native);\n    if (primary) IUnknown_Release(primary);'],
];
function body(source,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,d=1;for(;d;end++){if(source[end]==='{')d++;if(source[end]==='}')d--;}return{start:m.index,end,text:source.slice(m.index,end)};}
try{for(const[label,target,name,from,to]of rows){const source=sources[target],b=body(source,name);assert.equal(b.text.split(from).length,2,label);const file=dir+'/mutated.c';fs.writeFileSync(file,source.slice(0,b.start)+b.text.replace(from,to)+source.slice(b.end));const key=target==='main'?'DDRAW_PLATFORM_SOURCE':target==='surface'?'DDRAW_SURFACE_CONTROL_INCLUDE':'DDRAW_PLATFORM_INCLUDE',r=spawnSync(process.execPath,[__dirname+'/ddraw_surface_control_test.js'],{encoding:'utf8',env:{...process.env,[key]:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/platform line \d+:/,label+': strict compile and runtime assertion required');assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log('KILL '+label);}console.log(`PASS native surface-control mutations:${rows.length}/${rows.length} compiled implementations rejected at runtime`);}finally{fs.rmSync(dir,{recursive:true,force:true});}
