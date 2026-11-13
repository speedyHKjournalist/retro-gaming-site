'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(root+'/ddrawproxy/ddraw_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-metadata-mut-'));
const rows=[
 ['managed SetSurfaceDesc changes allocation','surface_SetSurfaceDesc','(surface->desc.ddsCaps.dwCaps2 &\n                (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE)) ||','FALSE ||','desc_policy','DDRAW_DESC_POLICY_SOURCE'],

 ['HEL lacks palette surface role','copy_caps_to_caller','DDSCAPS_PALETTE | DDSCAPS_PRIMARYSURFACE','DDSCAPS_PRIMARYSURFACE','caps_profile','DDRAW_CAPS_SOURCE'],
 ['HEL reports24bit depth','copy_caps_to_caller','caps.dwZBufferBitDepths = DDBD_16;','caps.dwZBufferBitDepths = DDBD_16 | DDBD_24;','caps_profile','DDRAW_CAPS_SOURCE'],
 ['HEL charges video memory','copy_caps_to_caller','caps.dwVidMemTotal = caps.dwVidMemFree = 0;','caps.dwVidMemTotal = caps.dwVidMemFree = 1024;','caps_profile','DDRAW_CAPS_SOURCE'],
 ['disabled HAL lacks NOHARDWARE','copy_caps_to_caller','if (profile == 3u) caps.dwCaps = DDCAPS_NOHARDWARE;','if (FALSE && profile == 3u) caps.dwCaps = DDCAPS_NOHARDWARE;','caps_profile','DDRAW_CAPS_SOURCE'],
 ['emulation factory publishes HAL','ddraw_GetCaps','object->factory_kind == DDRAW_FACTORY_EMULATION ? 3u : 0u','object->factory_kind == DDRAW_FACTORY_EMULATION ? 0u : 0u','caps_profile','DDRAW_CAPS_SOURCE'],
 ['hardware factory publishes HEL','ddraw_GetCaps','object->factory_kind == DDRAW_FACTORY_HARDWARE ? 2u : 1u','object->factory_kind == DDRAW_FACTORY_HARDWARE ? 1u : 1u','caps_profile','DDRAW_CAPS_SOURCE'],
 ['short prefix overwritten','copy_caps_to_caller','caller_size < sizeof(caps) ? caller_size : sizeof(caps)','caller_size < sizeof(caps) ? sizeof(caps) : sizeof(caps)','caps_profile','DDRAW_CAPS_SOURCE'],
 ['caller caps version lost','copy_caps_to_caller','destination->dwSize = caller_size;','destination->dwSize = sizeof(caps);','caps_profile','DDRAW_CAPS_SOURCE'],
 ['missing GDI output not cleared','ddraw_GetGDISurface','*out = NULL;','(void)out;','gdi_identity','DDRAW_GDI_IDENTITY_SOURCE'],
 ['GDI allocation no longer followed','ddraw_GetGDISurface','if (surface->memory != object->gdi_memory)','if (FALSE && surface->memory != object->gdi_memory)','gdi_identity','DDRAW_GDI_IDENTITY_SOURCE'],
 ['GDI interface returned without reference','ddraw_GetGDISurface','IDirectDrawSurface7_AddRef(*out);','(void)&IDirectDrawSurface7_AddRef;','gdi_identity','DDRAW_GDI_IDENTITY_SOURCE'],
 ['plain primary accepted by GDI flip','ddraw_FlipToGDISurface','if (!primary->front_of_chain) return DDWG_TRACE_ERROR(DDERR_NOTFLIPPABLE);','if (!primary->front_of_chain) return DD_OK;','gdi_identity','DDRAW_GDI_IDENTITY_SOURCE'],
 ['lost primary accepted by GDI flip','ddraw_FlipToGDISurface','if (FAILED(surface_IsLost','if (FALSE && FAILED(surface_IsLost','gdi_identity','DDRAW_GDI_IDENTITY_SOURCE'],
 ['GDI flip uses wrong wait flags','ddraw_FlipToGDISurface','(IDirectDrawSurface7 *)&walk->vtbl7, DDFLIP_WAIT','(IDirectDrawSurface7 *)&walk->vtbl7, 0','gdi_identity','DDRAW_GDI_IDENTITY_SOURCE'],
];
function body(name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,d=1;for(;d;end++){if(source[end]==='{')d++;if(source[end]==='}')d--;}return{start:m.index,end,text:source.slice(m.index,end)};}
try{for(const[label,name,from,to,harness,key]of rows){const b=body(name);assert.equal(b.text.split(from).length,2,label);const file=dir+'/mutated.c';fs.writeFileSync(file,source.slice(0,b.start)+b.text.replace(from,to)+source.slice(b.end));const r=spawnSync(process.execPath,[__dirname+'/ddraw_'+harness+'_test.js'],{encoding:'utf8',env:{...process.env,[key]:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/(?:caps profile|gdi identity|desc policy) line \d+:/,label+': strict compile and runtime assertion required');assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log('KILL '+label);}console.log('PASS DDraw compatibility metadata mutations:'+rows.length+'/'+rows.length+' compiled implementations rejected at runtime');}finally{fs.rmSync(dir,{recursive:true,force:true});}
