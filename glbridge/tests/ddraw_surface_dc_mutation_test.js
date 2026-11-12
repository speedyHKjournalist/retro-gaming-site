"use strict";
const fs=require("node:fs"),os=require("node:os"),path=require("node:path"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.resolve(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8").replace(/\\\n/g,"\n"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-dc-mut-"));
const cases=[
 ['NULL DC matches inactive surface','ddraw_surface_from_dc','if (!dc) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);','if (0 && !dc) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);'],
 ['private lookup pin missing','ddraw_surface_from_dc','surface_private_addref(found);','/* missing allocation pin */'],
 ['callback public ref missing','ddraw_surface_from_dc','surface_public_addref(found, view);','if (0) surface_public_addref(found, view);'],
 ['display allocation failure leaks pin','ddraw_surface_from_dc','surface_private_release(found);\n            return','/* leaked pin */\n            return'],
 ['native display DC replaced by memory DC','surface_lookup_device_dc','((DDrawNativeDC *)surface->gdi_dc)->screen','surface->dc'],
 ['display cache bypassed','surface_lookup_device_dc','if (!surface->lookup_screen_dc)','if (1)'],
 ['cached display DC double released','surface_release_lookup_dc','surface->lookup_screen_dc = NULL;','/* cached released handle retained */'],
 ['NULL surface output clears device output','GetSurfaceFromDC','if (!out) return DDWG_TRACE_ERROR(E_INVALIDARG);','if (!out) { if (device_dc) *device_dc = NULL; return DDWG_TRACE_ERROR(E_INVALIDARG); }'],
 ['NULL device output leaves stale surface','GetSurfaceFromDC','*out = NULL;','/* stale output */'],
 ['legacy method returns Surface7','tag##_GetSurfaceFromDC','DD_VERSION_1, NULL','DD_VERSION_7, NULL'],
];
function span(name){const m=new RegExp('(?:static )?(?:HRESULT|void|HDC) [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return[m.index,end];}
try{for(const[label,name,from,to]of cases){const[start,end]=span(name),chunk=source.slice(start,end);assert(chunk.includes(from),label);const file=dir+'/mutant.c';fs.writeFileSync(file,source.slice(0,start)+chunk.replace(from,to)+source.slice(end));const r=spawnSync(process.execPath,[__dirname+'/ddraw_surface_dc_test.js'],{encoding:'utf8',env:{...process.env,DDRAW_DC_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/surface DC line|AddressSanitizer|UndefinedBehaviorSanitizer/,label+': must compile then fail runtime checks\n'+r.stderr.slice(-1600));assert(!/:\d+:\d+: (?:fatal )?error:/.test(r.stderr),r.stderr);console.log('KILL '+label);}console.log('PASS surface-DC mutations: '+cases.length+'/'+cases.length+' compiled implementations rejected');}finally{fs.rmSync(dir,{recursive:true,force:true});}
