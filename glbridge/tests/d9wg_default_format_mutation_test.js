"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-default-format-mut-"));
function mutate(s, name, old, replacement) {
    const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(s); assert(m, name);
    let end = s.indexOf("{", m.index) + 1, depth = 1;
    for (; depth; end++) {if (s[end] === "{") depth++; if (s[end] === "}") depth--;}
    const body = s.slice(m.index, end); assert(body.includes(old), name + ": " + old);
    return s.slice(0, m.index) + body.replace(old, replacement) + s.slice(end);
}
function run(label,test,env){const r=spawnSync(process.execPath,[__dirname+'/'+test],{encoding:'utf8',timeout:30000,env:{...process.env,...env}}),out=r.stdout+r.stderr;assert(!r.error,label+': '+r.error);assert.notEqual(r.status,0,label+' survived');assert(!/error:|undefined reference|Undefined symbols/.test(out),label+' compile failure: '+out);assert(/Display mode|Present geometry|Additional parameters|Reset status|Assertion|signal=/.test(out));}
try {
 const header=fs.readFileSync(root+'/runtime/d9wg_display_mode.h','utf8');let killed=0;
 for(const[label,old,replacement]of[
  ['UNKNOWN forced to 32 bit','return mode.format;','return 22;'],
  ['explicit format overwritten','if (requested)\n        return requested;','if (0 && requested)\n        return requested;'],
  ['explicit formats poll Win32','if (requested)\n        return requested;','if (requested) { d9wg_display_mode_current(&mode); return requested; }'],
 ]){assert(header.includes(old),label);const file=dir+'/mode.h';fs.writeFileSync(file,header.replace(old,replacement));run(label,'d9wg_display_mode_test.js',{D9WG_DISPLAY_HEADER:file});killed++;}
 const source=fs.readFileSync(root+'/d3d9proxy/d3d9_proxy.c','utf8');
 for(const[fn,p,output,test,env]of[
  ['d3d_create_device','parameters','backbuffer_format','d9wg_present_geometry_entry_test.js','D9WG_GEOMETRY_SOURCE'],
  ['device_reset','parameters','backbuffer_format','d9wg_reset_status_test.js','D9WG_RESET_STATUS_FRONTEND'],
  ['device_create_additional_swap_chain','params','format','d9wg_additional_parameters_entry_test.js','D9WG_ADDITIONAL_PARAMETERS_SOURCE'],
 ]){
  const call='d9wg_display_mode_backbuffer_format(\n        '+p+'->BackBufferFormat)';
  const changes=[
   ['stale/constant fallback',((fn==='d3d_create_device'||fn==='device_reset')?'(D3DFORMAT)'+call:output+' = (D3DFORMAT)'+call+';'),((fn==='d3d_create_device'||fn==='device_reset')?'('+p+'->BackBufferFormat ? '+p+'->BackBufferFormat : '+(fn==='device_reset'?'device->display_mode.Format':'D3DFMT_X8R8G8B8')+')':output+' = '+p+'->BackBufferFormat ? '+p+'->BackBufferFormat : device->display_mode.Format;')],
   ['explicit format forced UNKNOWN',call,'d9wg_display_mode_backbuffer_format(\n        0)'],
  ];
  if(fn==='d3d_create_device')changes.push(['resolved format not cached','        backbuffer_format);','        D3DFMT_X8R8G8B8);']);
  else if(fn==='device_reset')changes.push(['resolved format not sent','    reset.backbuffer_format = backbuffer_format;','    reset.backbuffer_format = 22;']);
  else changes.push(['resolved format not returned','        width, height, format, window);','        width, height, 22, window);']);
  if(fn!=='device_create_additional_swap_chain')changes.push(['fullscreen UNKNOWN accepted','    if ('+'api == 9 && '+'!parameters->Windowed && parameters->BackBufferFormat == D3DFMT_UNKNOWN)\n        return TRACE_REFUSE(D3DERR_INVALIDCALL);','']);
  for(const[label,old,replacement]of changes){const file=dir+'/frontend.c';fs.writeFileSync(file,mutate(source,fn,old,replacement));run(fn+' '+label,test,{[env]:file});killed++;}
 }
 console.log('PASS default-format mutations: '+killed+'/14; live UNKNOWN, explicit no-query preservation, all three frontend uses and fullscreen guards');
} finally {fs.rmSync(dir,{recursive:true,force:true});}
