"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-native-user-planes-"));
const reference = JSON.parse(fs.readFileSync(path.join(__dirname, "fixtures/d9wg_d3d7_native_user_plane_reference.json")));
try {
    const input = path.join(directory, "reference.c"), binary = path.join(directory, "reference");
    fs.writeFileSync(input, `#include <stdio.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_math.h"
static const uint32_t expected[][4]={${reference.rows.map(r => "{" + r.words.map(x => x + "u").join(",") + "}").join(",")}};
static const float input[5][3]=${JSON.stringify(reference.input).replaceAll("[", "{").replaceAll("]", "}")};
static const float planes[6][4]=${JSON.stringify(reference.planes).replaceAll("[", "{").replaceAll("]", "}")};
int main(void){
 D9WGProcessLayout source,destination;d9wg_process_fvf(7,2,&source);d9wg_process_fvf(7,4,&destination);
 D9WGProcessViewport vp={7,11,100,80,.25f,.75f};D9WGTNLLighting lighting={0};uint32_t states[256]={0},row=0;
 for(unsigned transform=0;transform<3;++transform)for(unsigned plane=0;plane<6;++plane)for(unsigned mode=0;mode<4;++mode){
  float world[16]={0},view[16]={0},projection[16]={0},wv[16],wvp[16],output[5][4];
  for(unsigned i=0;i<4;++i)world[i*5]=view[i*5]=projection[i*5]=1;
  if(transform){world[12]=.25f;world[5]=2;}if(transform==2){view[12]=-.25f;view[10]=2;projection[15]=2;}
  d9wg_multiply_matrix(world,view,wv);d9wg_multiply_matrix(wv,projection,wvp);
  D9WGVertexState state;d9wg_vertex_state_init(&state,states);D9WGClipVertex records[5];
  if(mode!=3){state.clip_vertices=records;state.clip_guard_band[0]=state.clip_guard_band[1]=-4096;state.clip_guard_band[2]=state.clip_guard_band[3]=4095;}
  d9wg_vertex_clip_planes_init(&state,mode?1u<<plane:0,planes,world);
  if(!d9wg_process_vertices(&source,input,sizeof(input),0,12,0,&destination,output,sizeof(output),0,5,wv,wvp,&vp,&lighting,1,&state))return 1;
  for(unsigned v=0;v<5;++v,++row)for(unsigned c=0;c<4;++c){uint32_t actual;__builtin_memcpy(&actual,&output[v][c],4);
   if(actual!=expected[row][c]){fprintf(stderr,"native user-plane mismatch transform=%u plane=%u mode=%u vertex=%u channel=%u %08x != %08x\\n",transform,plane,mode,v,c,actual,expected[row][c]);return 1;}}
 }
 puts("PASS native Microsoft D3D7 user planes: 360 bit-exact outputs, world space, boundaries, OP_CLIP and RS_CLIPPING");return 0;
}
`);
    const sources = ["math", "clip", "blend", "vertex", "fvf", "lighting", "tnl_math"].map(name => process.env["D9WG_" + name.toUpperCase() + "_SOURCE"] || path.join(root, "runtime/d9wg_" + name + ".c"));
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root, "-I", path.join(root, "runtime"), input, ...sources, "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 3000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1500));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
