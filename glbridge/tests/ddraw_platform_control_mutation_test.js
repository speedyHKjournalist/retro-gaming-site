"use strict";
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),main=fs.readFileSync(root+'/ddrawproxy/ddraw_proxy.c','utf8'),platform=fs.readFileSync(root+'/ddrawproxy/ddraw_platform_control.inc','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-platform-mut-'));
const rows=[
 ['selected exclusive mode overwritten by desktop','include','ddraw_platform_sync','if (object->mode_set) return;','if (FALSE && object->mode_set) return;'],

 ['failed screen DC accepted','include','ddraw_platform_desktop_size','if (!screen) return FALSE;','if (FALSE && !screen) return FALSE;'],
 ['screen DC leaked','include','ddraw_platform_desktop_size','ReleaseDC(NULL, screen);','(void)&ReleaseDC;'],

 ['desktop width validity ignored','include','ddraw_platform_desktop_size','screen_width <= 0','FALSE'],
 ['desktop height validity ignored','include','ddraw_platform_desktop_size','screen_height <= 0','FALSE'],
 ['invalid physical pixel depth accepted','include','ddraw_platform_desktop_size','bits != 8 && bits != 16 && bits != 24 && bits != 32','FALSE && bits != 8 && bits != 16 && bits != 24 && bits != 32'],

 ['unavailable Draw3 does not use Draw2','platform','ddraw_platform_view','version == DD_VERSION_3 && *hr == E_NOINTERFACE','FALSE && *hr == E_NOINTERFACE'],

 ['cooperative hold omitted','main','ddraw_SetCooperativeLevelForVersion','InterlockedIncrement(&object->lifetime_ref);','(void)object;'],
 ['mode hold omitted','main','ddraw_SetDisplayModeForVersion','InterlockedIncrement(&object->lifetime_ref);','(void)object;'],
 ['restore hold omitted','main','ddraw_RestoreDisplayModeForVersion','InterlockedIncrement(&object->lifetime_ref);','(void)object;'],
 ['failed cooperative state committed','main','ddraw_SetCooperativeLevelForVersion','object->window = old_window;\n            object->cooperative_flags = old_flags;','object->window = window;\n            object->cooperative_flags = old_flags;'],
 ['failed mode state committed','main','ddraw_SetDisplayModeForVersion','ddraw_platform_load_mode(object, &old);','(void)old;'],
 ['native outer cooperative completion discarded','main','ddraw_SetCooperativeLevelForVersion','object->cooperative_flags = flags;\n        if (!(flags', 'object->cooperative_flags = old_flags;\n        if (!(flags'],
 ['nested mode emits stale state','main','ddraw_SetDisplayModeForVersion','if (serial != object->control_serial)\n            return ddraw_control_finish(object, hr);','if (FALSE && serial != object->control_serial)\n            return ddraw_control_finish(object, hr);'],
 ['normal keeps virtual mode','main','ddraw_SetCooperativeLevelForVersion','object->mode_set = FALSE;','object->mode_set = TRUE;'],
 ['restore keeps old mode','main','ddraw_RestoreDisplayModeForVersion','object->mode_set = FALSE;','object->mode_set = TRUE;'],
 ['unsupported virtual mode rejected','main','ddraw_SetDisplayModeForVersion','hr != E_NOTIMPL && hr != DDERR_UNSUPPORTEDMODE','hr != DDERR_UNSUPPORTEDMODE'],
 ['v7 cooperative routed as legacy','main','ddraw_SetCooperativeLevel','DD_VERSION_7','DD_VERSION_1'],
 ['v7 mode routed as legacy','main','ddraw_SetDisplayMode','DD_VERSION_7','DD_VERSION_1'],
 ['v7 restore routed as legacy','main','ddraw_RestoreDisplayMode','DD_VERSION_7','DD_VERSION_1'],
 ['native cooperative state ignored','main','ddraw_TestCooperativeLevel','return DDWG_TRACE_RESULT(hr);','return DD_OK;'],
 ['attachment loss always cleared','main','surface_IsLost','surface->cooperative_lost ? DDERR_SURFACELOST : DD_OK','surface->cooperative_lost ? DD_OK : DD_OK'],
 ['restore backend bypassed','main','surface_Restore','hr = ddraw_platform_restore_surface(surface);','hr = DD_OK;'],
 ['native control retained after public zero','main','ddraw_Release','if (!remaining && ddraw_platform_release(object))','if (FALSE && !remaining && ddraw_platform_release(object))'],
 ['retired primary keeps showing','include','ddraw_platform_release','object->primary = NULL;','(void)object->primary;'],
 ['retired control restores desktop again','include','ddraw_platform_release','object->guest_mode_changed = FALSE;','object->guest_mode_changed = TRUE;'],
 ['private lifetime leaked','include','ddraw_control_finish','ddraw_lifetime_release(object);','(void)object;'],
 ['factory kind ignored','include','ddraw_platform_attach','if (object->extended_factory)','if (FALSE && object->extended_factory)'],
 ['duplicate attach creates native object','include','ddraw_platform_attach','|| object->platform_ddraw','|| FALSE'],
 ['self DLL recursion allowed','include','ddraw_platform_attach','if (module == g_instance)','if (FALSE && module == g_instance)'],
 ['native interface mismatch','include','ddraw_platform_cooperative','case DD_VERSION_2: *hr = IDirectDraw2_SetCooperativeLevel','case DD_VERSION_2: *hr = IDirectDraw_SetCooperativeLevel'],
 ['virtual mode queries physical desktop','include','ddraw_platform_mode','if (object->mode_set && !object->guest_mode_changed)','if (FALSE && object->mode_set && !object->guest_mode_changed)'],
 ['system allocations lose state','include','ddraw_platform_lose_video_surfaces','surface->desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY','surface->desc.ddsCaps.dwCaps & (DDSCAPS_VIDEOMEMORY | DDSCAPS_SYSTEMMEMORY)'],
 ['managed allocations lose state','include','ddraw_platform_lose_video_surfaces','!(surface->desc.ddsCaps.dwCaps2 &','(TRUE || surface->desc.ddsCaps.dwCaps2) && !(0u &'],
];
function body(source,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,d=1;for(;d;end++){if(source[end]==='{')d++;if(source[end]==='}')d--;}return {start:m.index,end,text:source.slice(m.index,end)};}
try{for(const[label,target,name,from,to]of rows){const source=target==='main'?main:platform,b=body(source,name);assert.equal(b.text.split(from).length,2,label);const mutated=source.slice(0,b.start)+b.text.replace(from,to)+source.slice(b.end),file=dir+'/mutation.c';fs.writeFileSync(file,mutated);const r=spawnSync(process.execPath,[__dirname+'/ddraw_platform_control_test.js'],{encoding:'utf8',env:{...process.env,[target==='main'?'DDRAW_PLATFORM_SOURCE':'DDRAW_PLATFORM_INCLUDE']:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/platform line \d+:/,label+': compiled runtime assertion required');assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log('KILL '+label);}console.log(`PASS platform-control mutations: ${rows.length}/${rows.length} compiled implementations rejected at runtime`);}finally{fs.rmSync(dir,{recursive:true,force:true});}
