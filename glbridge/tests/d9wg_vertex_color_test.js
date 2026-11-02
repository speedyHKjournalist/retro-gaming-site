"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os");
const {spawnSync}=require("node:child_process"),{createHash}=require("node:crypto");
const root=path.resolve(__dirname,".."),fixture=require(path.resolve(root,"../../v86/tests/glbridge/fixtures/d3d_vertex_formats_cpu.json"));
const fromBits=bits=>{const b=Buffer.alloc(4);b.writeUInt32LE(bits);return b.readFloatLE();};
const cases=fixture.cases.map(r=>({...r}));
for(let type=0;type<4;++type){const bytes=Buffer.alloc((type+1)*4),value=[.0011,.0041,.0069,.5];for(let c=0;c<=type;++c)bytes.writeFloatLE(value[c],c*4);const values=[0,0,0,1];for(let c=0;c<=type;++c)values[c]=bytes.readFloatLE(c*4);cases.push({type,bytes:[...bytes],expected:values.map(n=>{const b=Buffer.alloc(4);b.writeFloatLE(n);return b.readUInt32LE();})});}
const quantize=n=>!(n>0)?0:n>=1?255:Math.floor(n*255+.5);
const pack=v=>((quantize(v[3])<<24)|(quantize(v[0])<<16)|(quantize(v[1])<<8)|quantize(v[2]))>>>0;
const expected=cases.map(r=>{const v=r.expected.map(fromBits),lit=v.map((n,i)=>i<3?Math.fround(n*100):n);return [[pack(v),pack(v)],[pack(lit),pack([0,0,0,v[3]])],[pack([0,0,0,v[3]]),pack(lit)],[pack(v),((pack(v)&0xffffff)|0x80000000)>>>0],[0xa5a5a5a5,0xa5a5a5a5],[0xa5a5a5a5,0x80a5a5a5]];});
const code=`#include <stdint.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_math.h"
#ifndef COLOR_CAPTURE
#define COLOR_CAPTURE(r,m,d,s) ((void)0)
#endif
#define CCHECK(x) do {if(!(x))return __LINE__;}while(0)
static const struct {uint32_t type;uint8_t bytes[16];uint32_t expected[6][2];} color_rows[]={
${cases.map((r,i)=>`{${r.type},{${r.bytes.join(",")}},{${expected[i].map(w=>"{"+w.map(n=>n+"u").join(",")+"}").join(",")}}}`).join(",\n")}
};
static int color_word_near(uint32_t actual,uint32_t expected,unsigned tolerance){for(unsigned i=0;i<4;++i){int d=(int)(actual>>(i*8)&255)-(int)(expected>>(i*8)&255);if(d<0)d=-d;if((unsigned)d>tolerance)return 0;}return 1;}
static __attribute__((noinline)) int vertex_color_cases(void){
 float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};D9WGProcessViewport vp={0,0,64,64,0,1};
 static D9WGTNLLighting lighting;D9WGProcessLayout destination;CCHECK(d9wg_process_fvf(9,0xc4,&destination));
 for(unsigned row=0;row<sizeof(color_rows)/sizeof(color_rows[0]);++row){
  D9WGVertexElement elements[]={{0,1,2,0,0,0},{0,14,2,0,3,0},{0,29,0,0,10,0},{0,46,0,0,10,1}};elements[2].type=elements[3].type=color_rows[row].type;
  D9WGProcessLayout source;CCHECK(d9wg_process_layout(elements,4,&source));
  uint8_t input[83];__builtin_memset(input,0,sizeof(input));float pos[3]={0,0,.5f},normal[3]={0,0,-1};
  __builtin_memcpy(input+1,pos,12);__builtin_memcpy(input+14,normal,12);__builtin_memcpy(input+29,color_rows[row].bytes,d9wg_decl_bytes(color_rows[row].type));__builtin_memcpy(input+46,color_rows[row].bytes,d9wg_decl_bytes(color_rows[row].type));
  for(unsigned mode=0;mode<6;++mode){
   D9WGTNLLightingFlags flags={0};flags.lighting=mode==1||mode==2;flags.specular_enable=mode==2;flags.color_vertex=1;flags.diffuse_source=1;flags.specular_source=2;
   D9WGSetMaterial material={0};for(unsigned i=0;i<4;++i)material.diffuse[i]=material.specular[i]=1;material.power=1;
   d9wg_lighting_init(&lighting,&material,0,&flags);d9wg_lighting_geometry(&lighting,identity);
   D9WGSetLight light={0};light.type=3;light.direction[2]=1;for(unsigned i=0;i<3;++i){light.diffuse[i]=mode==1?100:0;light.specular[i]=mode==2?100:0;}CCHECK(d9wg_lighting_add(&lighting,&light,identity));
   static D9WGVertexState state;__builtin_memset(&state,0,sizeof(state));if(mode==3||mode==5){state.fog_mode=3;state.fog_end=1;state.fog_density=1;}
   uint8_t output[72];__builtin_memset(output,0xa5,sizeof(output));
   CCHECK(d9wg_process_vertices(&source,input,sizeof(input),0,sizeof(input),0,&destination,output,sizeof(output),1,1,identity,identity,&vp,&lighting,mode<4,&state));
   uint32_t diffuse,specular;__builtin_memcpy(&diffuse,output+40,4);__builtin_memcpy(&specular,output+44,4);
   unsigned tolerance=mode==1||mode==2?1:0;
   if(!color_word_near(diffuse,color_rows[row].expected[mode][0],tolerance)||!color_word_near(specular,color_rows[row].expected[mode][1],tolerance)){COLOR_ERROR(row,mode,diffuse,specular,color_rows[row].expected[mode][0],color_rows[row].expected[mode][1]);return __LINE__;}
   for(unsigned i=0;i<24;++i)CCHECK(output[i]==0xa5);
   for(unsigned i=48;i<72;++i)CCHECK(output[i]==0xa5);
   COLOR_CAPTURE(row,mode,diffuse,specular);
  }
 }
 uint32_t special[4]={0x7fc00000u,0x7f800000u,0xff800000u,0x80000000u};float values[4];__builtin_memcpy(values,special,16);CCHECK(d9wg_pack_color(values)==0x0000ff00u);
 return 0;
}
#undef CCHECK
`;
if(process.env.D9WG_VERTEX_COLOR_HEADER_OUT){fs.writeFileSync(process.env.D9WG_VERTEX_COLOR_HEADER_OUT,"#define COLOR_ERROR(a,b,c,d,e,f) ((void)0)\n"+code);console.log("PASS generated typed color cases: "+cases.length);}
else{
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-colors-"));
 try{
  const file=path.join(directory,"color.c"),binary=path.join(directory,"color");
  fs.writeFileSync(file,'#include <stdio.h>\nstatic unsigned captured['+cases.length+'][6][2];\n#define COLOR_CAPTURE(r,m,d,s) do{captured[r][m][0]=d;captured[r][m][1]=s;}while(0)\n#define COLOR_ERROR(r,m,d,s,a,b) fprintf(stderr,"color row %u mode %u: %08x,%08x != %08x,%08x\\n",r,m,d,s,a,b)\n'+code+'int main(void){int e=vertex_color_cases();if(e){fprintf(stderr,"vertex color mismatch header line %d\\n",e);return 1;}printf("[");for(unsigned i=0;i<'+cases.length+';++i){printf("%s[",i?",":"");for(unsigned m=0;m<6;++m)printf("%s[%u,%u]",m?",":"",captured[i][m][0],captured[i][m][1]);printf("]");}puts("]");return 0;}');
  const sources=["clip","blend","vertex","fvf","math","lighting","tnl_math"].map(n=>process.env['D9WG_'+n.toUpperCase()+'_SOURCE']||path.join(root,"runtime/d9wg_"+n+".c"));
  const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-ffp-contract=off",...(process.env.D9WG_SANITIZE==="1"?["-fsanitize=undefined,float-cast-overflow","-fno-sanitize-recover=all"]:[]),"-Wall","-Wextra","-Werror","-I",root,"-I",path.join(root,"runtime"),file,...sources,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr.slice(0,3000)||String(built.error));
  const result=spawnSync(binary,[],{encoding:"utf8"});assert.equal(result.status,0,result.stderr.slice(0,1800)||String(result.error));const captures=JSON.parse(result.stdout);assert.equal(captures.length,cases.length);
  if(process.argv.includes("--write-fixture")){
   const selected=[];for(let type=0;type<17;++type){const index=cases.findIndex((r,i)=>r.type===type&&i%64>=4&&r.expected.map(fromBits).every(Number.isFinite));assert(index>=0);selected.push(index);}for(let i=1088;i<cases.length;++i)selected.push(i);
   const output={sourceSha256:Object.fromEntries(["vertex.c","vertex.h","lighting.c","lighting.h","math.c","math.h","vertex_format.h"].map(p=>["runtime/d9wg_"+p,createHash("sha256").update(fs.readFileSync(path.join(root,"runtime/d9wg_"+p))).digest("hex")])),cases:selected.flatMap(index=>[0,1,2,3].map(mode=>{
    const bounded=mode===3?cases.findIndex(r=>r.type===cases[index].type&&r.expected.map(fromBits).every(n=>Number.isFinite(n)&&n>=0&&n<=1)):index;assert(bounded>=0);const r=cases[bounded];
    return {name:"type-"+r.type+"-sample-"+index+"-row-"+bounded+"-mode-"+mode,type:r.type,mode,bytes:r.bytes,decoded:r.expected,processedColors:captures[bounded][mode]};
   }))};
   fs.writeFileSync(path.resolve(root,"../../v86/tests/glbridge/fixtures/d3d_software_vertex_colors_cpu.json"),JSON.stringify(output,null,2)+"\n");
  }
  console.log("PASS typed CPU colors: "+cases.length+" inputs / six copy-lighting-specular-fog modes, precision before quantization, atomic guards and defined nonfinite packing");
 }finally{fs.rmSync(directory,{recursive:true,force:true});}
}
