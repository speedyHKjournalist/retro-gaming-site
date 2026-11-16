"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),fixture=require(path.resolve(root,"../../v86/tests/glbridge/fixtures/d3d_vertex_formats_cpu.json"));
const fromBits=n=>{const b=Buffer.alloc(4);b.writeUInt32LE(n);return b.readFloatLE();},components=[1,2,3,4,4,4,2,4,4,2,4,2,4,3,3,2,4];
const rows=fixture.cases.filter(r=>r.expected.map(fromBits).every(Number.isFinite));
for(let type=0;type<4;++type){const bytes=Buffer.alloc((type+1)*4),values=[.0011,.0041,.0069,.5],expected=[0,0,0,0x3f800000];for(let c=0;c<=type;++c){bytes.writeFloatLE(values[c],c*4);expected[c]=bytes.readUInt32LE(c*4);}rows.push({type,bytes:[...bytes],expected});}
const code=`#include <stdint.h>
#include "runtime/d9wg_clip_draw.h"
#define TCCHECK(x) do {if(!(x))return __LINE__;}while(0)
static const struct TypedClipRow {uint32_t type,components;uint8_t bytes[16];uint32_t expected[4];} typed_clip_rows[]={
${rows.map(r=>`{${r.type},${components[r.type]},{${r.bytes.join(",")}},{${r.expected.map(n=>n+"u").join(",")}}}`).join(",\n")}
};
typedef struct TypedClipCapture {const D9WGProcessLayout *layout;const struct TypedClipRow *row;unsigned role,count,failed;} TypedClipCapture;
static int typed_clip_capture(void *context,uint32_t primitive,const void *vertices,uint32_t count,uint32_t stride){
 TypedClipCapture *capture=context;const D9WGProcessLayout *layout=capture->layout;const struct TypedClipRow *row=capture->row;
 if(primitive!=4)return 0;
 int32_t offset=capture->role==0?layout->diffuse:capture->role==1?layout->specular:capture->role==2?layout->normal:capture->role==3?layout->psize:layout->texcoord[0];
 if(offset<0)return 0;
 for(uint32_t v=0;v<count;++v){const uint8_t *attribute=(const uint8_t*)vertices+v*stride+offset;
  if(capture->role<2&&row->type==4){uint32_t got,wanted;__builtin_memcpy(&got,attribute,4);__builtin_memcpy(&wanted,row->bytes,4);if(got!=wanted){capture->failed=1;return 0;}}
  else{unsigned components=capture->role<2?4:capture->role==2?3:capture->role==3?1:row->components;for(unsigned c=0;c<components;++c){float got,wanted;__builtin_memcpy(&got,attribute+c*4,4);__builtin_memcpy(&wanted,row->expected+c,4);if(got!=wanted){capture->failed=c+1;return 0;}}}
 }
 capture->count+=count;return 1;
}
static __attribute__((noinline)) int typed_clip_cases(void){
 D9WGProcessViewport vp={0,0,64,64,0,1};float guard[4]={-4096,-4096,4095,4095},clip[3][4]={{-2,-.5f,.5f,1},{.5f,-.5f,.5f,1},{0,.75f,-.25f,1}};
 static union {uint64_t alignment;uint8_t bytes[1024];} storage;D9WGClipBatch *batch=(D9WGClipBatch*)storage.bytes;
 TCCHECK(d9wg_clip_batch_init(batch,sizeof(storage),0,16,3,&vp));
 float positions[3][4];for(unsigned v=0;v<3;++v){d9wg_clip_vertex(clip[v],&vp,guard,1,batch->vertex+v);__builtin_memcpy(positions[v],batch->vertex[v].stored,16);}batch->live=3;D9WGClipBuffer buffer={batch};
 static D9WGProcessStreamLayout input;static D9WGClipWork work;static uint8_t staging[65552];
 for(unsigned row=0;row<sizeof(typed_clip_rows)/sizeof(typed_clip_rows[0]);++row)for(unsigned role=0;role<5;++role){
  const struct TypedClipRow *r=typed_clip_rows+row;D9WGVertexElement elements[]={{3,0,3,0,9,0},{15,1,0,0,0,0}};
  elements[1].type=r->type;elements[1].usage=role<2?10:role==2?3:role==3?4:5;elements[1].usage_index=role==1;
  TCCHECK(d9wg_process_stream_layout(elements,2,&input));
  D9WGVertexElement output_elements[2];D9WGProcessLayout output;TCCHECK(d9wg_clip_stream_output(&input,output_elements,&output));
  TCCHECK(output_elements[1].type==(role<2?(r->type==4?4:3):role==2?2:role==3?0:r->components-1));
  TCCHECK(output_elements[1].stream==0&&output_elements[1].offset==16);
  uint8_t attribute[24];__builtin_memset(attribute,0x63,sizeof(attribute));__builtin_memcpy(attribute+4,r->bytes,d9wg_decl_bytes(r->type));
  D9WGVertexStream streams[16]={{0}};streams[3]=(D9WGVertexStream){(const uint8_t*)positions,sizeof(positions),0,16};streams[15]=(D9WGVertexStream){attribute,4+d9wg_decl_bytes(r->type),3,0};
  D9WGClipDrawSource source={0};source.clips=&buffer;source.count=source.vertex_count=3;source.primitive_type=4;source.viewport=vp;
  d9wg_clip_draw_streams_init(&source,&input,streams);source.output_layout=&output;if(source.stride<output.stride)source.stride=output.stride;
  TypedClipCapture capture={&output,r,role,0,0};__builtin_memset(staging,0xa5,sizeof(staging));
  int result=d9wg_clip_draw(&source,&work,staging,65536,typed_clip_capture,&capture);
  if(result!=1||capture.count<3||capture.failed){TYPED_CLIP_ERROR(row,role,capture.failed,result);return __LINE__;}
  for(unsigned i=65536;i<sizeof(staging);++i)TCCHECK(staging[i]==0xa5);
 }
 return 0;
}
#undef TCCHECK
`;
if(process.env.D9WG_TYPED_CLIP_HEADER_OUT){fs.writeFileSync(process.env.D9WG_TYPED_CLIP_HEADER_OUT,"#define TYPED_CLIP_ERROR(a,b,c,d) ((void)0)\n"+code);console.log("PASS generated typed clipping cases: "+rows.length+" finite inputs / five roles");}
else{
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-typed-clip-"));
 try{
  const input=path.join(directory,"clip.c"),binary=path.join(directory,"clip");fs.writeFileSync(input,'#include <stdio.h>\n#define TYPED_CLIP_ERROR(r,m,c,e) fprintf(stderr,"typed clip row %u role %u component %u result %d\\n",r,m,c,e)\n'+code+'int main(void){int error=typed_clip_cases();if(error){fprintf(stderr,"typed clipping mismatch line %d\\n",error);return 1;}return 0;}');
  const sources=["clip","clip_draw","blend","vertex","fvf","math","lighting","tnl_math"].map(n=>process.env['D9WG_'+n.toUpperCase()+'_SOURCE']||path.join(root,"runtime/d9wg_"+n+".c"));
  const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-ffp-contract=off","-Wall","-Wextra","-Werror","-I",root,"-I",path.join(root,"runtime"),input,...sources,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr.slice(0,3000)||String(built.error));
  const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr.slice(0,1500)||String(run.error));
  console.log("PASS typed clipping: "+rows.length+" finite inputs / "+rows.length*5+" role cases, all 17 types, float colors/specular, packed normal/size/coordinates, expanded wire output, native DWORD path and destination guards");
 }finally{fs.rmSync(directory,{recursive:true,force:true});}
}
