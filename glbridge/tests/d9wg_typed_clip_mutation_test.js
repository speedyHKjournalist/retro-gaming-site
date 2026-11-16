"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-typed-clip-mutants-"));
const mutations=[
 ["float colours quantized before interpolation","clip_draw","d9wg_decl_read(type,source+offset,out);return;","d9wg_decl_read(type,source+offset,out);uint32_t quantized=d9wg_pack_color(out);d9wg_lighting_color(quantized,out);return;"],
 ["float specular emitted as an ARGB word","clip_draw","if(layout->specular_type==3)__builtin_memcpy","if(layout->specular_type==3 && 0)__builtin_memcpy"],
 ["packed normal uses a different type","clip_draw","d9wg_decl_read(layout->normal_type,data+layout->normal,value)","d9wg_decl_read(8,data+layout->normal,value)"],
 ["packed point size treated as float","clip_draw","d9wg_decl_read(layout->psize_type,data+layout->psize,value)","d9wg_decl_read(0,data+layout->psize,value)"],
 ["texture loses extra components","clip_draw","d9wg_decl_read(layout->texcoord_type[t],data+layout->texcoord[t],value)","d9wg_decl_read(0,data+layout->texcoord[t],value)"],
 ["float colour output declaration remains packed","vertex","element->usage_index < 2 && element->type != 4) element->type = 3;","element->usage_index < 2 && element->type != 4) element->type = 4;"],
 ["native DWORD output accidentally widened","vertex","element->usage_index < 2 && element->type != 4) element->type = 3;","element->usage_index < 2) element->type = 3;"],
];
try{
 for(const [label,name,before,after] of mutations){const original=fs.readFileSync(path.join(root,"runtime/d9wg_"+name+".c"),"utf8");assert.equal(original.split(before).length,2,"unique mutation: "+label);const file=path.join(directory,name+".c");fs.writeFileSync(file,original.replace(before,after));
  const result=spawnSync(process.execPath,[path.join(__dirname,"d9wg_typed_clip_test.js")],{encoding:"utf8",env:{...process.env,["D9WG_"+name.toUpperCase()+"_SOURCE"]:file}});assert.notEqual(result.status,0,"survived: "+label);assert.match(result.stderr,/typed clipping mismatch line/,"must execute an assertion: "+label);
 }
 console.log("PASS typed clipping semantic mutations: 7/7; production sources untouched");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
