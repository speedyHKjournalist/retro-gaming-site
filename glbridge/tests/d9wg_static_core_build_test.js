"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{execFileSync:run}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-static-core-")),compiler=process.env.D9WG_CC||"i686-w64-mingw32-gcc",archiver=process.env.AR||"i686-w64-mingw32-ar",nm=process.env.NM||"i686-w64-mingw32-nm",objdump=process.env.OBJDUMP||"i686-w64-mingw32-objdump";
const invoke=(command,args)=>run(command,args,{encoding:"utf8",stdio:["ignore","pipe","pipe"]});
try {
    let sourceRoot=root;
    if(process.env.D9WG_STATIC_CORE_SOURCE){sourceRoot=dir+"/package";fs.mkdirSync(sourceRoot+"/d3d9proxy",{recursive:true});for(const file of fs.readdirSync(root)){if(file!=="d3d9proxy")fs.symlinkSync(root+"/"+file,sourceRoot+"/"+file);}for(const file of fs.readdirSync(root+"/d3d9proxy")){if(file!=="d3d9_proxy.c")fs.symlinkSync(root+"/d3d9proxy/"+file,sourceRoot+"/d3d9proxy/"+file);}fs.copyFileSync(process.env.D9WG_STATIC_CORE_SOURCE,sourceRoot+"/d3d9proxy/d3d9_proxy.c");}
    for(const diagnostic of[false,true]){
        const archive=dir+(diagnostic?"/diagnostic.a":"/retail.a");invoke("sh",[sourceRoot+"/d3d9proxy/build.sh",diagnostic?"--static-diagnostic":"--static",archive]);
        const members=invoke(archiver,["t",archive]).trim().split("\n");assert.equal(members.length,21);assert.equal(new Set(members).size,21);assert(members.includes("d3d9_core.o")&&members.includes("d9wg_process.o"));
        const symbols=invoke(nm,["-g","--defined-only",archive]);assert.match(symbols,/ T _d9wg_d3d9_core_create@4\b/,"Missing private factory ABI");assert.match(symbols,/ T _d9wg_d3d9_core_module@12\b/,"Missing private loader ABI");assert(!/ T _(Direct3DCreate9|DllMain|DebugSetMute|D3DPERF_\w+)@/.test(symbols),"Private archive leaked public DLL symbols");
        const dll=dir+(diagnostic?"/diagnostic.dll":"/retail.dll");invoke(compiler,["-shared","-std=gnu99","-Os","-s","-nostdlib","-Wall","-Wextra","-Werror","-Wl,--subsystem,windows:5.01","-Wl,-e,_DllMain@12","-Wl,--kill-at","-Wl,--exclude-all-symbols","-o",dll,root+"/sample/d3d9_core_consumer.c",root+"/sample/d3d9_core_consumer.def",archive,"-luser32","-lgdi32","-lkernel32"]);
        const pe=invoke(objdump,["-p",dll]),imports=[...pe.matchAll(/DLL Name: (\S+)/g)].map(m=>m[1].toLowerCase());assert.deepEqual(imports.sort(),["gdi32.dll","kernel32.dll","user32.dll"]);const table=pe.split("[Ordinal/Name Pointer] Table").pop();assert(table);const exports=[...table.split("\n\n")[0].matchAll(/^\s*\[\s*\d+\]\s+(?:\+base\[\s*\d+\]\s+[0-9a-fA-F]+\s+)?(\S+)\s*$/gm)].map(m=>m[1]);assert.deepEqual(exports,["D9CoreCreate9"]);
        if(diagnostic){const bytes=fs.readFileSync(dll);assert(bytes.includes(Buffer.from("d3d9_core_trace_%lu.log\0")));assert(!bytes.includes(Buffer.from("d3d9_trace_%lu.log\0")),"Embedded trace must not overwrite ordinary D3D9 trace");}
    }
    const abi=dir+"/d3d8_abi.c";fs.writeFileSync(abi,'#define COBJMACROS\n#include <d3d8.h>\n#include "'+root+'/d3d9proxy/d3d9_core.h"\nvoid* core_factory(void){return d9wg_d3d9_core_create(32);}\n');invoke(compiler,["-std=gnu99","-Os","-Wall","-Wextra","-Werror","-c",abi,"-o",dir+"/d3d8_abi.o"]);
    console.log("PASS private D3D9 static core: retail/diagnostic21-member archives, prefixed stdcall factory/loader ABI, no public loader exports, strict CRT-free consumer DLLs, D3D8 SDK header coexistence and separate diagnostic trace path");
} finally {fs.rmSync(dir,{recursive:true,force:true});}
