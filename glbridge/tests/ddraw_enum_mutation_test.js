"use strict";
const fs=require("node:fs"),os=require("node:os"),path=require("node:path"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.resolve(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-enum-mut-"));
const cases=[
 ["current mode cancellation ignored","if (callback(&description, context) == DDENUMRET_CANCEL) break;","if (callback(&description, context) == -12345) break;"],
 ["legacy mode cancellation ignored","if (callback(&description, context) == DDENUMRET_CANCEL) break; "+String.fromCharCode(92),"if (callback(&description, context) == -12345) break; "+String.fromCharCode(92)],
 ["24-bit modes omitted","bytes_per_pixel = mode.dmBitsPerPel / 8u;","if (mode.dmBitsPerPel == 24u) continue; bytes_per_pixel = mode.dmBitsPerPel / 8u;"],
 ["default enumeration repeats refresh variants","if (!(flags & DDEDM_REFRESHRATES)) {","if (FALSE && !(flags & DDEDM_REFRESHRATES)) {"],
 ["default enum hardcodes refresh rate","? mode.dmDisplayFrequency : 0u;","? mode.dmDisplayFrequency : 60u;"],
 ["driver width ignored","description->dwWidth = mode.dmPelsWidth;","description->dwWidth = 640u;"],
 ["driver mode query bypassed","if (!EnumDisplaySettingsA(NULL, current, &mode)) return FALSE;","if (TRUE || !EnumDisplaySettingsA(NULL, current, &mode)) return FALSE;"],
 ["current driver refresh ignored","return mode.dmDisplayFrequency;","return 60u;"],
 ["explicit refresh discarded","description->dwRefreshRate = object->mode_refresh ? object->mode_refresh\n        : ddraw_desktop_refresh_rate();","description->dwRefreshRate = ddraw_desktop_refresh_rate();"],
 ["flag combinations accepted","(selection && (selection & (selection - 1u)))","(FALSE && selection && (selection & (selection - 1u)))"],
 ["CANBECREATED allows ALL","((flags & DDENUMSURFACES_CANBECREATED) && selection != DDENUMSURFACES_MATCH)","FALSE"],
 ["dimension pair not validated","(!!(filter->dwFlags & DDSD_WIDTH) != !!(filter->dwFlags & DDSD_HEIGHT))","FALSE"],
 ["existing dimensions ignored","actual->dwWidth != filter->dwWidth","FALSE"],
 ["NOMATCH not inverted","match == (selection == DDENUMSURFACES_NOMATCH)","!match"],
 ["partial caps accepted","actual->ddsCaps.dwCaps != filter->ddsCaps.dwCaps","(actual->ddsCaps.dwCaps & filter->ddsCaps.dwCaps) != filter->ddsCaps.dwCaps"],
 ["creatable dimensions ignored","if (explicit_dimensions) {\n                width = request.dwWidth;","if (explicit_dimensions) {\n                width = 64u;"],
 ["unsupported format accepted","if (!bytes_per_pixel && !block_bytes) return FALSE;","if (!bytes_per_pixel && !block_bytes) { bytes_per_pixel = 4; }"],
 ["surface callback cancellation ignored","callback(items[index].surface, &items[index].description, context) == DDENUMRET_CANCEL) break;","callback(items[index].surface, &items[index].description, context) == -12345) break;"],
 ["snapshot lifetime not retained","surface_private_addref(surface);\n    }\n    LeaveCriticalSection(&g_object_lock);","/* missing pin */\n    }\n    LeaveCriticalSection(&g_object_lock);"],
 ["snapshot lifetime leaked","for (index = 0; index < count; ++index) surface_private_release(items[index].surface);","/* missing release */"],
 ["current callback ref omitted","if (surface) surface_public_addref(surface, DD_VERSION_7);","/* missing public ref */"],
 ["legacy callback ref in wrong view","surface_public_addref(surface, surface_factory_view(VERSION)); \\","surface_public_addref(surface, DD_VERSION_7); \\"],
 ["current display format ignored","!ddraw_enum_pixel_format_matches(&description.ddpfPixelFormat, &filter->ddpfPixelFormat)) continue;","FALSE) continue;"],
 ["legacy display format ignored","!ddraw_enum_pixel_format_matches(&description.ddpfPixelFormat, &filter->ddpfPixelFormat)) continue; \\","FALSE) continue; \\"],
];
try{for(const[label,from,to]of cases){assert(source.includes(from),label);const file=dir+"/mutant.c";fs.writeFileSync(file,source.replace(from,to));const r=spawnSync(process.execPath,[__dirname+"/ddraw_enum_test.js"],{encoding:"utf8",env:{...process.env,DDRAW_ENUM_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/enum line|AddressSanitizer: heap-use-after-free/,label+": must compile then fail runtime assertions\n"+r.stderr.slice(-2000));assert(!/error: /.test(r.stderr),r.stderr);console.log("KILL "+label);}console.log("PASS surface enumeration mutations: "+cases.length+"/"+cases.length+" compiled implementations rejected");}finally{fs.rmSync(dir,{recursive:true,force:true});}
