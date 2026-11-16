"use strict";
// Execute the actual core policy against SDK9 constants;the protocol guard
// compares these operand shapes with the independent host parser.
const assert=require("node:assert/strict"),fs=require("node:fs"),os=require("node:os"),path=require("node:path"),{spawnSync}=require("node:child_process");
module.exports=function opcodePolicy(root){
    const source=fs.readFileSync(root+"/d3d9proxy/d3d9_proxy.c","utf8"),dir=fs.mkdtempSync(path.join(os.tmpdir(),"shader8-opcode-"));
    function extract(s,name){const m=new RegExp("^[A-Za-z_][^;{}\\n]*?\\b"+name+"\\([^;]*?\\)\\s*\\{","m").exec(s);assert(m,name);let end=s.indexOf("{",m.index)+1,depth=1;for(;depth;end++){if(s[end]==="{")depth++;if(s[end]==="}")depth--;}return s.slice(m.index,end);}
    try{
        fs.writeFileSync(dir+"/sdk.c","#include <d3d9.h>\n"+extract(source,"shader8_opcode_supported"));
        const p=spawnSync(process.env.D9WG_CC||"i686-w64-mingw32-gcc",["-E","-P",dir+"/sdk.c"],{encoding:"utf8",maxBuffer:16<<20});assert.equal(p.status,0,p.stderr);
        const enumeration=p.stdout.match(/typedef enum _D3DSHADER_INSTRUCTION_OPCODE_TYPE\s*\{[\s\S]*?\}\s*D3DSHADER_INSTRUCTION_OPCODE_TYPE\s*;/);assert(enumeration);
        const names=[...enumeration[0].matchAll(/\b(D3DSIO_\w+)\s*=/g)].map(m=>m[1]);assert(names.length>80);
        fs.writeFileSync(dir+"/policy.c",`#include <stdint.h>\n#include <stdio.h>\ntypedef uint16_t WORD;typedef uint32_t UINT;typedef int BOOL;\n${enumeration[0]}\n${extract(p.stdout,"shader8_opcode_supported")}\nint main(void){const struct{const char*name;UINT op;}ops[]={${names.map(n=>'{"'+n.slice(7).toUpperCase()+'",'+n+'}').join(",")}};for(UINT i=0;i<sizeof(ops)/sizeof(ops[0]);i++)for(UINT pixel=0;pixel<2;pixel++)for(UINT minor=0;minor<=(pixel?4u:1u);minor++){UINT words=0;if(shader8_opcode_supported((WORD)ops[i].op,pixel,minor,&words))printf("%s %u\\n",ops[i].name,words);}return 0;}\n`);
        const b=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",dir+"/policy.c","-o",dir+"/policy"],{encoding:"utf8"});assert.equal(b.status,0,b.stderr);
        const run=spawnSync(dir+"/policy",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);const values=new Map();
        for(const line of run.stdout.trim().split("\n")){const[name,word]=line.split(" ");assert(name&&Number.isInteger(Number(word)));if(!values.has(name))values.set(name,new Set());values.get(name).add(Number(word));}
        return new Map([...values].map(([name,words])=>[name,words.size===1?[...words][0]:null]));
    }finally{fs.rmSync(dir,{recursive:true,force:true});}
};
