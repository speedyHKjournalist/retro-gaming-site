"use strict";
const fs=require("node:fs"),os=require("node:os"),path=require("node:path"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.resolve(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-enum-mut-"));
const cases=[
 ["flag combinations accepted","(selection && (selection & (selection - 1u)))","(FALSE && selection && (selection & (selection - 1u)))"],
 ["CANBECREATED allows ALL","((flags & DDENUMSURFACES_CANBECREATED) && selection != DDENUMSURFACES_MATCH)","FALSE"],
 ["dimension pair not validated","(!!(filter->dwFlags & DDSD_WIDTH) != !!(filter->dwFlags & DDSD_HEIGHT))","FALSE"],
 ["existing dimensions ignored","actual->dwWidth != filter->dwWidth","FALSE"],
 ["NOMATCH not inverted","match == (selection == DDENUMSURFACES_NOMATCH)","!match"],
 ["partial caps accepted","actual->ddsCaps.dwCaps != filter->ddsCaps.dwCaps","(actual->ddsCaps.dwCaps & filter->ddsCaps.dwCaps) != filter->ddsCaps.dwCaps"],
 ["creatable dimensions ignored","request.dwFlags & DDSD_WIDTH ? request.dwWidth : g_modes[mode].width","g_modes[mode].width"],
 ["unsupported format accepted","if (!bytes_per_pixel && !block_bytes) return FALSE;","if (!bytes_per_pixel && !block_bytes) { bytes_per_pixel = 4; }"],
 ["callback cancellation ignored","== DDENUMRET_CANCEL) break;","== -12345) break;"],
 ["snapshot lifetime not retained","surface_private_addref(surface);\n    }\n    LeaveCriticalSection(&g_object_lock);","/* missing pin */\n    }\n    LeaveCriticalSection(&g_object_lock);"],
 ["snapshot lifetime leaked","for (index = 0; index < count; ++index) surface_private_release(items[index].surface);","/* missing release */"],
 ["current callback ref omitted","if (surface) surface_public_addref(surface, DD_VERSION_7);","/* missing public ref */"],
 ["legacy callback ref in wrong view","surface_public_addref(surface, surface_factory_view(VERSION)); \\","surface_public_addref(surface, DD_VERSION_7); \\"],
 ["current display format ignored","!ddraw_enum_pixel_format_matches(&description.ddpfPixelFormat, &filter->ddpfPixelFormat)) continue;","FALSE) continue;"],
 ["legacy display format ignored","!ddraw_enum_pixel_format_matches(&description.ddpfPixelFormat, &filter->ddpfPixelFormat)) continue; \\","FALSE) continue; \\"],
];
try{for(const[label,from,to]of cases){assert(source.includes(from),label);const file=dir+"/mutant.c";fs.writeFileSync(file,source.replace(from,to));const r=spawnSync(process.execPath,[__dirname+"/ddraw_enum_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_ENUM_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/enum line|AddressSanitizer: heap-use-after-free/,label+": must compile then fail runtime assertions\n"+r.stderr.slice(-2000));assert(!/error: /.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS surface enumeration mutations: "+cases.length+"/"+cases.length+" compiled implementations rejected");}finally{fs.rmSync(dir,{recursive:true,force:true});}
