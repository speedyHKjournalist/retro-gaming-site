"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os");
const {spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-color-mutants-"));
const mutations=[
 ["diffuse quantized before lighting","vertex","if (s->diffuse >= 0) d9wg_decl_read(s->diffuse_type, in + s->diffuse, color0);","if (s->diffuse >= 0) { d9wg_decl_read(s->diffuse_type, in + s->diffuse, color0); uint32_t packed = d9wg_pack_color(color0); d9wg_lighting_color(packed, color0); }"],
 ["specular quantized before lighting","vertex","if (s->specular >= 0) d9wg_decl_read(s->specular_type, in + s->specular, color1);","if (s->specular >= 0) { d9wg_decl_read(s->specular_type, in + s->specular, color1); uint32_t packed = d9wg_pack_color(color1); d9wg_lighting_color(packed, color1); }"],
 ["source specular alpha lost","lighting","out_specular[3] = color1 ? color1[3] : 0.0f;","out_specular[3] = 0.0f;"],
 ["DONOTCOPYDATA still copies float colors","vertex","(copy_data || lighting->flags.lighting)","(copy_data || lighting->flags.lighting || s->diffuse_type != 4)"],
 ["fog destroys preserved specular RGB","vertex","(specular & 0x00ffffffu) |","(specular & 0x00000000u) |"],
 ["NaN reaches integer conversion","math","if (!(value > 0.0f)) return 0;","if (value < 0.0f) return 0;",true],
];
try{
 for(const [label,name,before,after,sanitize] of mutations){
  const original=fs.readFileSync(path.join(root,"runtime/d9wg_"+name+".c"),"utf8");assert.equal(original.split(before).length,2,"unique color mutation: "+label);
  const file=path.join(directory,name+".c");fs.writeFileSync(file,original.replace(before,after));
  const result=spawnSync(process.execPath,[path.join(__dirname,"d9wg_vertex_color_test.js")],{encoding:"utf8",env:{...process.env,["D9WG_"+name.toUpperCase()+"_SOURCE"]:file,...(sanitize?{D9WG_SANITIZE:"1"}:{})}});
  assert.notEqual(result.status,0,"survived: "+label);assert.match(result.stderr,sanitize?/runtime error:.*range/:/vertex color mismatch header line/,"must execute the check: "+label);
 }
 console.log("PASS typed color semantic mutations: 6/6, including executed undefined-conversion sanitizer; production untouched");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
