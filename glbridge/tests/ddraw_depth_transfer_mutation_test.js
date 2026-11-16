'use strict';const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const file=path.resolve(__dirname,'../ddrawproxy/ddraw_proxy.c'),source=fs.readFileSync(file,'utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'ddraw-depth-transfer-mut-')),mutant=dir+'/ddraw_proxy.c';
const mutations=[
 ["legacy D24 packing omitted","to->ddpfPixelFormat.dwZBitMask = 0x00ffffffu;","to->ddpfPixelFormat.dwZBitMask = 0;"],
 ["depth-fill normalization uses wire mask","DWORD mask = surface_client_depth_mask(surface), shift = 0;","DWORD mask = surface_depth_mask(surface->format), shift = 0;"],
 ["32-bit storage confuses 24-bit precision","out->z_bit_depth = 24u;","out->z_bit_depth = 32u;"],
 ['readback translation absent','        surface_repack_depth_rows(surface, surface->shadow + first_row * surface->pitch,\n                surface->pitch, surface->width, chunk_rows, FALSE);',''],
 ['readback translates padding','surface->pitch, surface->width, chunk_rows, FALSE);','surface->pitch, surface->pitch / surface->bytes_per_pixel, chunk_rows, FALSE);'],
 ['upload direction reversed','right - left, rows, TRUE);','right - left, rows, FALSE);'],
 ['partial upload starts at x zero','source_x = left * surface->bytes_per_pixel;','source_x = 0;'],
 ['partial upload starts at y zero','source_y = top;','source_y = 0;'],
 ['temporary upload rows use client pitch','upload.source = converted;upload.source_pitch = row_bytes;','upload.source = converted;'],
 ['temporary upload storage leaked','    heap_free(converted);',''],
 ['allocation failure reports success','                return FALSE;\n            }\n            for (row = 0; row < rows; ++row)','                return TRUE;\n            }\n            for (row = 0; row < rows; ++row)'],
 ['depth fill uses wire mask','DWORD mask = surface_client_depth_mask(surface);','DWORD mask = surface_depth_mask(surface->format);'],
 ['stencil removed from native packing','*client_stencil = *wire_stencil ? pf->dwStencilBitMask : 0u;','*client_stencil = 0;'],
];
try{let killed=0;for(const[name,old,replacement]of mutations){assert.equal(source.split(old).length,2,name+' unique mutation');fs.writeFileSync(mutant,source.replace(old,replacement));const r=spawnSync(process.execPath,[__dirname+'/ddraw_depth_transfer_test.js'],{encoding:'utf8',env:{...process.env,DDRAW_DEPTH_LAYOUT_SOURCE:mutant,DDRAW_DEPTH_LAYOUT_INCLUDE_ROOT:path.dirname(file)},timeout:30000});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' compile failure '+output);assert(/depth transfer line|AddressSanitizer/.test(output),name+' no runtime assertion '+output);killed++;}console.log('PASS actual depth-transfer runtime mutations: '+killed+'/'+mutations.length);}finally{fs.rmSync(dir,{recursive:true,force:true});}
