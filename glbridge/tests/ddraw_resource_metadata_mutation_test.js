"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const source=fs.readFileSync(path.join(__dirname,"../ddrawproxy/ddraw_proxy.c"),"utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"ddraw-resource-mut-"));
const rows=[
 ["missing palette preserves stale output","surface_GetPalette","*out = NULL;","(void)out;"],
 ["palette getter omits AddRef","surface_GetPalette","IDirectDrawPalette_AddRef(*out);","(void)out;"],
 ["nontexture priority accepted","surface_priority_valid","if (!(surface->desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE))","if (FALSE && !(surface->desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE))"],
 ["ordinary texture priority accepted","surface_priority_valid","!(surface->desc.ddsCaps.dwCaps2 &\n            (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE))","FALSE"],
 ["mip priority accepted","surface_priority_valid","surface->mip_level || surface->cube_face","FALSE || surface->cube_face"],
 ["other cube face priority accepted","surface_priority_valid","surface->mip_level || surface->cube_face","surface->mip_level || FALSE"],
 ["priority setter forgets value","surface_SetPriority","surface->priority = priority;","(void)priority;"],
 ["priority getter always zero","surface_GetPriority","*priority = surface->priority;","*priority = 0;"],
 ["failed priority getter writes output","surface_GetPriority","if (FAILED(result)) return DDWG_TRACE_ERROR(result);","if (FAILED(result)) { *priority = 0; return DDWG_TRACE_ERROR(result); }"],
 ["failed priority setter changes state","surface_SetPriority","HRESULT result = surface_priority_valid(surface);","surface->priority = priority; HRESULT result = surface_priority_valid(surface);"],
 ["v7 palette accepts mip","surface_SetPaletteForVersion","version >= DD_VERSION_7 && surface->mip_level","version > DD_VERSION_7 && surface->mip_level"],
 ["v7 key accepts mip","surface_SetColorKeyForVersion","version >= DD_VERSION_7 && surface->mip_level","version > DD_VERSION_7 && surface->mip_level"],
 ["palette policy follows factory","surface_SetPaletteForVersion","version >= DD_VERSION_7 && surface->mip_level","(version && surface->creation_version >= DD_VERSION_7) && surface->mip_level"],
 ["key policy follows factory","surface_SetColorKeyForVersion","version >= DD_VERSION_7 && surface->mip_level","(version && surface->creation_version >= DD_VERSION_7) && surface->mip_level"],
 ["legacy palette setter uses v7 rule",null,"surface_SetPaletteForVersion(SURFACE7(iface), palette, VERSION)","surface_SetPaletteForVersion(SURFACE7(iface), palette, DD_VERSION_7)"],
 ["legacy key setter uses v7 rule",null,"surface_SetColorKeyForVersion(SURFACE7(iface), flags, key, VERSION)","surface_SetColorKeyForVersion(SURFACE7(iface), flags, key, DD_VERSION_7)"],
 ["RGB surface accepts palette","surface_SetPaletteForVersion","if (!surface->indexed)","if (FALSE && !surface->indexed)"],
 ["alpha palette allowed on offscreen","surface_SetPaletteForVersion","!(surface->desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE)","(FALSE && !(surface->desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE))"],
 ["empty palette detach reports success","surface_SetPaletteForVersion","DDWG_TRACE_ERROR(DDERR_NOPALETTEATTACHED)","DD_OK"],
 ["rejected palette gains reference","surface_SetPaletteForVersion","    if (palette) {\n        if ((palette->caps","    if (palette) { IDirectDrawPalette_AddRef(palette_iface);\n        if ((palette->caps"],
 ["palette replacement publishes after release","surface_SetPaletteForVersion","surface->palette = palette;\n    if (previous)\n        IDirectDrawPalette_Release((IDirectDrawPalette *)previous);","if (previous) IDirectDrawPalette_Release((IDirectDrawPalette *)previous);\n    surface->palette = palette;"],
];
try{for(const[label,name,from,to]of rows){let start=0,end=source.length;if(name){const m=new RegExp("static [^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{").exec(source);assert(m,name);start=m.index;end=source.indexOf("{",start)+1;let depth=1;for(;depth;end++){if(source[end]==="{")depth++;if(source[end]==="}")depth--;}}const body=source.slice(start,end);assert.equal(body.split(from).length,2,label);const file=dir+'/proxy.c';fs.writeFileSync(file,source.slice(0,start)+body.replace(from,to)+source.slice(end));const r=spawnSync(process.execPath,[__dirname+'/ddraw_resource_metadata_test.js'],{encoding:'utf8',env:{...process.env,DDRAW_METADATA_SOURCE:file}});assert.notEqual(r.status,0,label);assert.match(r.stderr,/resource metadata line/,label+': strict compile and runtime assertion required');assert(!/error: |ERROR: AddressSanitizer|runtime error:/.test(r.stderr),r.stderr);console.log('KILL '+label);}console.log('PASS resource metadata mutations: '+rows.length+'/'+rows.length+' compiled implementations rejected at runtime');}finally{fs.rmSync(dir,{recursive:true,force:true});}
