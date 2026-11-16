"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process"), { createHash } = require("node:crypto");
const root = path.resolve(__dirname, ".."), sizes = [4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8], counts = [1,2,3,4,4,4,2,4,4,2,4,2,4,3,3,2,4];
let seed = 0x714538ab;
const random = () => (seed = (Math.imul(seed,1664525)+1013904223) >>> 0);
const floatBits = n => {const b=Buffer.alloc(4);b.writeFloatLE(n);return b.readUInt32LE();};
function halfValue(n) {
    const negative = n >= 32768, exponent = (n >>> 10)&31, fraction = n&1023;
    const value = exponent === 31 ? fraction ? NaN : Infinity : (exponent ? 1+fraction/1024 : fraction/1024)*2**(exponent ? exponent-15 : -14);
    return negative ? -value : value;
}
function decode(type, bytes) {
    const out = [0,0,0,1], word = bytes.readUInt32LE();
    for(let i=0;i<counts[type];++i) {
        if(type<4) out[i]=bytes.readFloatLE(i*4);
        else if(type===4) out[i]=bytes[[2,1,0,3][i]]/255;
        else if(type===5 || type===8) out[i]=bytes[i]/(type===8?255:1);
        else if(type===13 || type===14) {
            const n = Math.floor(word/2**(i*10))%1024;
            out[i] = type===13 ? n : Math.max(-1,(n>=512?n-1024:n)/511);
        } else if(type>=15) out[i]=halfValue(bytes.readUInt16LE(i*2));
        else if(type===11 || type===12) out[i]=bytes.readUInt16LE(i*2)/65535;
        else {const n=bytes.readInt16LE(i*2);out[i]=type===9 || type===10 ? Math.max(-1,n/32767) : n;}
    }
    return out.map(Math.fround);
}
const cases=[];
for(let type=0;type<17;++type) for(let row=0;row<64;++row) {
    const bytes=Buffer.alloc(16);for(let j=0;j<4;++j)bytes.writeUInt32LE(random(),j*4);
    if(row===0)bytes.fill(0);if(row===1)bytes.fill(255);
    if(row===2)for(let j=0;j<8;++j)bytes.writeUInt16LE(0x8000,j*2);
    if(row===3)for(let j=0;j<8;++j)bytes.writeUInt16LE(0x7fff,j*2);
    if((type===13 || type===14) && row===4)bytes.writeUInt32LE(0x20080200);
    if(type<4)for(let j=0;j<4;++j)bytes.writeFloatLE(row===0?[-0,0,.5,1][j]:(random()%2049-1024)/32,j*4);
    cases.push({type,bytes:[...bytes.subarray(0,sizes[type])],expected:decode(type,bytes).map(floatBits)});
}
const halfExpected=Array.from({length:65536},(_,i)=>floatBits(halfValue(i)));
const code=`#include <stdint.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_math.h"
#include "runtime/d9wg_blend.h"
#include "runtime/d9wg_clip_draw.h"
#define FCHECK(x) do { if(!(x))return __LINE__; } while(0)
static const struct {uint32_t type;uint8_t bytes[16];uint32_t expected[4];} format_rows[]={
${cases.map(r=>`{${r.type},{${r.bytes.join(",")}},{${r.expected.map(n=>n+"u").join(",")}}}`).join(",\n")}
};
static const uint32_t half_expected[65536]={${halfExpected.map(n=>n+"u").join(",")}};
static int format_equal(float value,uint32_t expected) {
 uint32_t got;__builtin_memcpy(&got,&value,4);
 if((expected&0x7fffffffu)>0x7f800000u)return (got&0x7fffffffu)>0x7f800000u;
 return got==expected;
}
static int vertex_format_cases(void) {
 for(unsigned i=0;i<65536;++i){uint16_t halves[2]={(uint16_t)i,0};float values[4];d9wg_decl_read(15,halves,values);FCHECK(format_equal(values[0],half_expected[i]));}
 for(unsigned row=0;row<sizeof(format_rows)/sizeof(format_rows[0]);++row) {
  float values[4];uint8_t unaligned[17];__builtin_memcpy(unaligned+1,format_rows[row].bytes,16);
  FCHECK(d9wg_decl_read(format_rows[row].type,unaligned+1,values));
  for(unsigned i=0;i<4;++i)FCHECK(format_equal(values[i],format_rows[row].expected[i]));
 }
 float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
 D9WGProcessViewport vp={7,11,100,80,.25f,.75f};
 static D9WGTNLLighting light;D9WGSetMaterial material={0};D9WGTNLLightingFlags flags={0};
 d9wg_lighting_init(&light,&material,0,&flags);d9wg_lighting_geometry(&light,identity);
 D9WGProcessLayout dest;FCHECK(d9wg_process_fvf(9,0x201f4,&dest));
 for(unsigned row=0;row<sizeof(format_rows)/sizeof(format_rows[0]);++row) {
  if(row%64<4)continue;
  uint32_t type=format_rows[row].type;float decoded[4];int finite=1;
  __builtin_memcpy(decoded,format_rows[row].expected,16);
  for(unsigned i=0;i<4;++i)if((format_rows[row].expected[i]&0x7f800000u)==0x7f800000u)finite=0;
  if(!finite)continue;
  D9WGVertexElement elements[]={{0,1,3,0,0,0},{0,18,0,0,3,0},{0,35,0,0,4,0},{0,52,0,0,5,0}};
  elements[1].type=elements[2].type=elements[3].type=type;
  D9WGProcessLayout source;FCHECK(d9wg_process_layout(elements,4,&source));
  uint8_t input[83],output[64];__builtin_memset(input,0,sizeof(input));__builtin_memset(output,0xa5,sizeof(output));
  const float position[4]={.25f,-.5f,.5f,2};__builtin_memcpy(input+1,position,16);
  __builtin_memcpy(input+18,format_rows[row].bytes,d9wg_decl_bytes(type));
  __builtin_memcpy(input+35,format_rows[row].bytes,d9wg_decl_bytes(type));
  __builtin_memcpy(input+52,format_rows[row].bytes,d9wg_decl_bytes(type));
  FCHECK(d9wg_process_vertices(&source,input,sizeof(input),0,sizeof(input),0,&dest,output,dest.stride,0,1,identity,identity,&vp,&light,1,0));
  for(unsigned i=0;i<3;++i){float actual;__builtin_memcpy(&actual,output+dest.normal+i*4,4);FCHECK(format_equal(actual,format_rows[row].expected[i]));}
  float actual;__builtin_memcpy(&actual,output+dest.psize,4);FCHECK(format_equal(actual,format_rows[row].expected[0]));
  for(unsigned i=0;i<d9wg_decl_components(type);++i){__builtin_memcpy(&actual,output+dest.texcoord[0]+i*4,4);FCHECK(format_equal(actual,format_rows[row].expected[i]));}
  for(unsigned i=d9wg_decl_components(type);i<4;++i){uint32_t word;__builtin_memcpy(&word,output+dest.texcoord[0]+i*4,4);FCHECK(word==0xa5a5a5a5u);}
  /* Packed positions use the same decode/expansion as ordinary attributes. */
  D9WGVertexElement p={0,1,(uint8_t)type,0,0,0};FCHECK(d9wg_process_layout(&p,1,&source));
  __builtin_memcpy(input+1,format_rows[row].bytes,d9wg_decl_bytes(type));
  FCHECK(d9wg_process_vertices(&source,input,sizeof(input),0,sizeof(input),0,&dest,output,dest.stride,0,1,identity,identity,&vp,&light,0,0));
  float screen[4];d9wg_clip_project(decoded,&vp,screen);
  for(unsigned i=0;i<4;++i){uint32_t expected;__builtin_memcpy(&actual,output+i*4,4);__builtin_memcpy(&expected,screen+i,4);FCHECK(format_equal(actual,expected));}
  D9WGClipDrawSource draw={0};draw.vertices=input;draw.length=draw.stride=source.stride;draw.count=draw.vertex_count=1;draw.layout=&source;
  static D9WGVertexState state;__builtin_memset(&state,0,sizeof(state));D9WGClipStatus status;d9wg_clip_status_init(&status);
  FCHECK(d9wg_vertex_clip_codes(&draw,identity,&state,&status));
  FCHECK(status.clip_union==d9wg_clip_outcode(decoded)&&status.clip_intersection==status.clip_union);
 }
 /* Packed normals and normalized weights feed the same blend/light/clip core
  * as independently decoded float attributes. */
 {
  float worlds[32];__builtin_memcpy(worlds,identity,64);__builtin_memcpy(worlds+16,identity,64);worlds[16]=2;worlds[26]=2;worlds[28]=.25f;
  static uint64_t scratch[512];D9WGVertexBlend *blend=(D9WGVertexBlend *)scratch;
  FCHECK(d9wg_blend_init(blend,sizeof(scratch),1,0,worlds,2,0,identity,identity));
  D9WGVertexElement packed[]={{0,0,2,0,0,0},{0,12,14,0,3,0},{0,16,9,0,1,0}};
  D9WGVertexElement plain[]={{0,0,2,0,0,0},{0,12,2,0,3,0},{0,24,0,0,1,0}};
  uint8_t input[20]={0},a[32],b[32];float source[7]={.25f,.25f,.5f,-1,0,1,8192.0f/32767.0f};
  __builtin_memcpy(input,source,12);uint32_t normal=512u|(511u<<20);__builtin_memcpy(input+12,&normal,4);uint16_t weights[2]={8192,0};__builtin_memcpy(input+16,weights,4);
  D9WGProcessLayout packed_layout,plain_layout;FCHECK(d9wg_process_layout(packed,3,&packed_layout));FCHECK(d9wg_process_layout(plain,3,&plain_layout));
  FCHECK(d9wg_process_fvf(9,0x44,&dest));flags.lighting=1;material.diffuse[0]=material.diffuse[1]=material.diffuse[2]=material.diffuse[3]=1;
  d9wg_lighting_init(&light,&material,0,&flags);d9wg_lighting_geometry(&light,identity);
  D9WGSetLight lamp={0};lamp.type=3;lamp.diffuse[0]=lamp.diffuse[1]=lamp.diffuse[2]=1;lamp.direction[2]=-1;FCHECK(d9wg_lighting_add(&light,&lamp,identity));
  static D9WGVertexState state;__builtin_memset(&state,0,sizeof(state));state.blend=blend;D9WGClipVertex record[2];state.clip_vertices=record;state.clip_guard_band[0]=state.clip_guard_band[1]=-4096;state.clip_guard_band[2]=state.clip_guard_band[3]=4095;
  FCHECK(d9wg_process_vertices(&packed_layout,input,sizeof(input),0,sizeof(input),0,&dest,a,sizeof(a),0,1,identity,identity,&vp,&light,1,&state));
  state.clip_vertices=record+1;FCHECK(d9wg_process_vertices(&plain_layout,source,sizeof(source),0,sizeof(source),0,&dest,b,sizeof(b),0,1,identity,identity,&vp,&light,1,&state));
  for(unsigned i=0;i<dest.stride;++i)FCHECK(a[i]==b[i]);
  FCHECK(record[0].code==record[1].code);for(unsigned i=0;i<4;++i)FCHECK(record[0].stored[i]==record[1].stored[i]);
 }
 /* Unsupported packed output cannot partially change the destination. */
 {
  D9WGVertexElement output[]={{0,0,3,0,9,0},{0,16,16,0,5,0}};D9WGProcessLayout source;
  FCHECK(d9wg_process_layout(output,2,&dest));FCHECK(!d9wg_process_canonical_output(&dest));FCHECK(d9wg_process_fvf(9,2,&source));
  float input[3]={0,0,.5f};uint8_t result[32];__builtin_memset(result,0xa5,sizeof(result));
  FCHECK(!d9wg_process_vertices(&source,input,12,0,12,0,&dest,result,sizeof(result),0,1,identity,identity,&vp,&light,1,0));
  for(unsigned i=0;i<sizeof(result);++i)FCHECK(result[i]==0xa5);
 }
 FCHECK(!d9wg_decl_bytes(17)&&!d9wg_decl_components(17));float v[4];FCHECK(!d9wg_decl_read(17,format_rows,v));
 return 0;
}
#undef FCHECK
`;
if(process.env.D9WG_VERTEX_FORMAT_HEADER_OUT) {
    fs.writeFileSync(process.env.D9WG_VERTEX_FORMAT_HEADER_OUT,code);console.log("PASS generated portable vertex format cases: "+cases.length);
} else {
    const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-formats-"));
    try {
        const input=path.join(directory,"test.c"),binary=path.join(directory,"test");
        fs.writeFileSync(input,'#include <stdio.h>\n'+code+'int main(void){int e=vertex_format_cases();if(e){fprintf(stderr,"vertex format mismatch header line %d\\n",e);return 1;}puts("PASS 17 vertex input formats: 1088 independent vectors, all 65536 half patterns, unaligned geometry/normal/size/texture conversion and retained components");return 0;}');
        const sources=["clip","blend","vertex","fvf","math","lighting","tnl_math"].map(n=>path.join(root,"runtime/d9wg_"+n+".c"));
        const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-ffp-contract=off","-Wall","-Wextra","-Werror","-I",root,"-I",path.join(root,"runtime"),...(process.env.D9WG_VERTEX_FORMAT_SOURCE?["-include",process.env.D9WG_VERTEX_FORMAT_SOURCE]:[]),input,...sources,"-o",binary],{encoding:"utf8"});
        assert.equal(built.status,0,built.stderr.slice(0,3000)||String(built.error));
        const result=spawnSync(binary,[],{encoding:"utf8"});assert.equal(result.status,0,result.stderr.slice(0,1500)||String(result.error));process.stdout.write(result.stdout);
        if(process.argv.includes("--write-fixture")) {
            const fixture={sourceSha256:createHash("sha256").update(fs.readFileSync(path.join(root,"runtime/d9wg_vertex_format.h"))).digest("hex"),cases};
            fs.writeFileSync(path.resolve(root,"../../v86/tests/glbridge/fixtures/d3d_vertex_formats_cpu.json"),JSON.stringify(fixture,null,2)+"\n");
        }
    } finally {fs.rmSync(directory,{recursive:true,force:true});}
}
