"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-cursor-mut-")),header=readComparisonSource(root+"/runtime/d9wg_cursor_bridge.h","utf8");
const mutations=[
 ['X8 accepted','image->format == D3DFMT_A8R8G8B8','(image->format == D3DFMT_A8R8G8B8 || image->format == 22)'],
 ['locked source accepted','&& !image->locked','&& TRUE'],
 ['non-power width accepted','&& !(image->width & (image->width - 1))','&& TRUE'],
 ['non-power height accepted','&& !(image->height & (image->height - 1))','&& TRUE'],
 ['oversized adapter width accepted','&& image->width <= image->adapter_width','&& TRUE'],
 ['oversized adapter height accepted','&& image->height <= image->adapter_height','&& TRUE'],
 ['GPU source replaced by CPU mirror','if (image->readback)','if (0)'],
 ['readback uses destination device','request.device_handle = image->device;','request.device_handle = device_handle;'],
 ['readback loses mip','request.level = image->level;','request.level = 0;'],
 ['readback loses cube face','request.face = image->face;','request.face = 0;'],
 ['GPU temporary leaked','HeapFree(GetProcessHeap(), 0, temporary);','(void)temporary;'],
 ['wrong image device','command.device_handle = device_handle;\n        command.hotspot_x','command.device_handle = 0; (void)device_handle;\n        command.hotspot_x'],
 ['hotspot lost','command.hotspot_x = hotspot_x;','command.hotspot_x = 0; (void)hotspot_x;'],
 ['wrong blob offset','command.data_offset = (uint32_t)(blob - batch_base());','command.data_offset = 0;'],
 ['padded source treated as tight','pixels + row * pitch','pixels + row * row_bytes'],
 ['copy includes source padding','pixels + row * pitch, row_bytes);','pixels + row * pitch, pitch);'],
 ['small CPU storage accepted','image->height - 1u > (image->byte_count - row_bytes) / pitch','0'],
 ['failed reserve returns success','return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;','return D3D_OK;'],
 ['image never armed','cursor->ready = TRUE;','cursor->ready = FALSE;'],
 ['all sizes warp OS pointer','cursor->hardware = width == 32 && height == 32;','cursor->hardware = !!width && !!height;'],
 ['initial image never hides GDI fallback','return d9wg_cursor_visibility(device_handle, FALSE);','return TRUE; (void)device_handle;'],
 ['Show without properties enabled','if (!cursor->ready)\n        return FALSE;',''],
 ['Show returns current instead of previous','return previous;','return cursor->visible; (void)previous;'],
 ['position loses flags','command.flags = flags;','command.flags = 0; (void)flags;'],
 ['screen coordinates never converted','ScreenToClient(window, &point);','(void)window;'],
 ['hardware pointer never warped','SetCursorPos(x, y);','(void)x;'],
 ['OS motion ignored on Present','if (cursor->hardware) {','if (0) {'],
 ['software position overwritten on Present','if (cursor->hardware) {','if (cursor->ready) {'],
];
function run(name,env){const r=spawnSync(process.execPath,[__dirname+'/d9wg_cursor_test.js'],{encoding:'utf8',timeout:20000,env:{...process.env,...env}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' failed compile: '+output);assert(/Cursor API|Assertion|signal=/.test(output),name+' no runtime failure');}
function span(source,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(source);assert(m,name);let end=source.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}return[m.index,end];}
try{let killed=0;for(const[name,old,replacement]of mutations){assert(header.includes(old),name);const file=dir+'/candidate.h';let candidate=header.replace(old,replacement);if(name==='GPU temporary leaked')candidate=header.replaceAll(old,replacement);fs.writeFileSync(file,candidate);run(name,{D9WG_CURSOR_HEADER:file});killed++;}for(const api of[8,9])for(const name of['device_set_cursor_position','device_show_cursor']){const source=readComparisonSource(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8'),[a,b]=span(source,name),old='device->present.hDeviceWindow ? device->present.hDeviceWindow : device->creation.hFocusWindow',body=source.slice(a,b);assert(body.includes(old));const file=dir+'/candidate.c';fs.writeFileSync(file,source.slice(0,a)+body.replace(old,'device->present.hDeviceWindow')+source.slice(b));run('API '+api+' '+name+' loses focus-window fallback',{['D'+api+'WG_CURSOR_SOURCE']:file});killed++;}console.log('PASS shared cursor mutations: '+killed+'/'+(mutations.length+4)+'; image validation/rows/ownership, GPU source identity, lifetime/error cleanup, Show, signed coordinate conversion, hardware/software position and four public window-fallback paths');}finally{fs.rmSync(dir,{recursive:true,force:true});}
