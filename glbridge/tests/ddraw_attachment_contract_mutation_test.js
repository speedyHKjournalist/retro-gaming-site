'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const source=fs.readFileSync(path.join(__dirname,'../ddrawproxy/ddraw_proxy.c'),'utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-attachment-contract-mutants-'));
const cases=[
 ['depth width check omitted','surface->width != other->width || surface->height != other->height)\n            return DDWG_TRACE_ERROR(DDERR_CANNOTATTACHSURFACE);','FALSE || surface->height != other->height)\n            return DDWG_TRACE_ERROR(DDERR_CANNOTATTACHSURFACE);'],
 ['all depth sizes accepted','if (surface->width != other->width || surface->height != other->height)\n            return DDWG_TRACE_ERROR(DDERR_CANNOTATTACHSURFACE);','if (FALSE)\n            return DDWG_TRACE_ERROR(DDERR_CANNOTATTACHSURFACE);'],
 ['attachment namespace not saved','surface->attached_depth_namespace = version;','surface->attached_depth_namespace = DD_VERSION_7;'],
 ['cross-view duplicate reports already attached','surface->attached_depth_namespace == version\n                    ? DDERR_SURFACEALREADYATTACHED : DDERR_CANNOTATTACHSURFACE','TRUE\n                    ? DDERR_SURFACEALREADYATTACHED : DDERR_CANNOTATTACHSURFACE'],
 ['duplicate in its own view rejected as foreign','surface->attached_depth_namespace == version\n                    ? DDERR_SURFACEALREADYATTACHED : DDERR_CANNOTATTACHSURFACE','FALSE\n                    ? DDERR_SURFACEALREADYATTACHED : DDERR_CANNOTATTACHSURFACE'],
 ['delete ignores namespace','if (surface->attached_depth_namespace != version)','if (version > DD_VERSION_7)'],
 ['matching delete rejected','if (surface->attached_depth_namespace != version)','if (version <= DD_VERSION_7)'],
 ['held depth reference changed to caller7','surface->attached_depth_version = other_version;','surface->attached_depth_version = DD_VERSION_7;'],
 ['depth add leaks held reference','surface->attached_depth_namespace = version;\n        surface_public_addref(other, other_version);','surface->attached_depth_namespace = version;\n        (void)other_version;'],
 ['depth delete leaks held reference','surface_public_release(other, held_version);','(void)held_version;'],
 ['inherited getters return Surface2/3','unsigned returned_version = surface_factory_view(VERSION); \\\n        surface_transfer_public_ref','unsigned returned_version = VERSION; \\\n        surface_transfer_public_ref'],
 ['inherited enumeration returns Surface2/3','unsigned returned_version = surface_factory_view(VERSION); \\\n    if (!callback)','unsigned returned_version = VERSION; \\\n    if (!callback)'],
 ['depth enumeration omits callback reference','surface_public_addref(surface->attached_depth, returned_version);','(void)returned_version;'],
 ['legacy delete uses modern namespace','return surface_DeleteAttachedSurfaceForVersion(SURFACE7(iface), flags, \\\n            other ? (IDirectDrawSurface7 *)other : NULL, VERSION);','return surface_DeleteAttachedSurfaceForVersion(SURFACE7(iface), flags, \\\n            other ? (IDirectDrawSurface7 *)other : NULL, DD_VERSION_7);'],
];
try{for(const[name,before,after]of cases){assert.equal(source.split(before).length,2,name);const file=dir+'/mutant.c';fs.writeFileSync(file,source.replace(before,after));const r=spawnSync(process.execPath,[path.join(__dirname,'ddraw_attachment_contract_test.js')],{encoding:'utf8',env:{...process.env,DDRAW_ATTACHMENT_SOURCE:file}});assert.notEqual(r.status,0,'survived '+name);assert(/attachment contract line \d+:/.test(r.stderr),'compiled runtime assertion required: '+name+'\n'+r.stderr);assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log('KILL '+name);}console.log('PASS actual attachment contract faults: '+cases.length+'/'+cases.length);}finally{fs.rmSync(dir,{recursive:true,force:true});}
