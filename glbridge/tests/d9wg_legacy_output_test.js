"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), reference = require("./fixtures/d9wg_d3d7_native_layout_reference.json");
const rows = reference.rows.filter(row => row.hr === 0);
assert(rows.length >= 32, "native reference must contain executed, successful transformations");
const code = `#include <stdint.h>
#include "runtime/d9wg_vertex.h"
#include "runtime/d9wg_math.h"
#define LCHECK(x) do { if (!(x)) return __LINE__; } while (0)
static const struct { uint32_t source,fvf,stride,mode,edge,length;uint32_t words[77]; } legacy_rows[] = {
${rows.map(r => `{${r.source},${r.fvf},${r.stride},${r.mode},${r.edge},${r.words.length},{${r.words.map(n => n + "u").join(",")}}}`).join(",\n")}
};
static int legacy_output_cases(void)
{
    const uint32_t formats[]={2,0x1d2,0x201d2},strides[]={12,40,48};
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},projection[16];
    __builtin_memcpy(projection,identity,sizeof(identity));projection[15]=2;
    D9WGProcessViewport vp={7,11,100,80,.25f,.75f};
    static D9WGTNLLighting light;D9WGSetMaterial material={0};D9WGTNLLightingFlags flags={0};
    d9wg_lighting_init(&light,&material,0,&flags);d9wg_lighting_geometry(&light,identity);
    for (unsigned row=0;row<sizeof(legacy_rows)/sizeof(legacy_rows[0]);++row) {
        D9WGProcessLayout source,dest;
        LCHECK(d9wg_process_fvf(7,formats[legacy_rows[row].source],&source));
        LCHECK(d9wg_process_legacy_output(legacy_rows[row].fvf,&dest));
        LCHECK(dest.stride==legacy_rows[row].stride);
        uint32_t input[84],output[88];
        for(unsigned i=0;i<88;++i)output[i]=0xa5000000u+i;
        for(unsigned i=0;i<7;++i) {
            float v[12]={.25f*((int)i-2),.25f*(3-(int)i),.125f*i,.25f*i,-.5f,.75f,0,0,.125f*i,.25f,.5f,.75f};
            uint32_t *to=input+i*strides[legacy_rows[row].source]/4;
            __builtin_memcpy(to,v,strides[legacy_rows[row].source]);
            if(legacy_rows[row].source){to[6]=0x81234560u+i;to[7]=0x596789a0u+i;}
        }
        D9WGVertexState state={0};D9WGClipVertex records[3];
        if(legacy_rows[row].mode&2) {state.clip_vertices=records;state.clip_guard_band[0]=state.clip_guard_band[1]=-4096;state.clip_guard_band[2]=state.clip_guard_band[3]=4095;}
        LCHECK(d9wg_process_vertices(&source,input,7*source.stride,0,source.stride,1,&dest,output,7*dest.stride,
            legacy_rows[row].edge?5:1,legacy_rows[row].edge?1:3,identity,projection,&vp,&light,!(legacy_rows[row].mode&1),&state));
        for(unsigned i=0;i<legacy_rows[row].length;++i) {
            if(output[i]!=legacy_rows[row].words[i]) { LEGACY_ERROR(row,i,output[i],legacy_rows[row].words[i]);return __LINE__; }
        }
        for(unsigned i=legacy_rows[row].length;i<88;++i)LCHECK(output[i]==0xa5000000u+i);
        for(unsigned i=0;i<88;++i)output[i]=0xa5000000u+i;
        LCHECK(!d9wg_process_vertices(&source,input,7*source.stride,0,source.stride,1,&dest,output,7*dest.stride-1,
            6,1,identity,projection,&vp,&light,1,&state));
        for(unsigned i=0;i<88;++i)LCHECK(output[i]==0xa5000000u+i);
    }
    /* Final valid vertices must not touch storage outside the nominal buffer,
     * even when native RGB would spill RHW / copied attributes there. */
    const uint32_t tail_formats[]={2,0x42,0x82,0xc2,0x30102,0x102};
    for(unsigned i=0;i<sizeof(tail_formats)/sizeof(tail_formats[0]);++i) {
        D9WGProcessLayout s,d;uint32_t input[10]={0},output[32];
        LCHECK(d9wg_process_fvf(7,0x1d2,&s));LCHECK(d9wg_process_legacy_output(tail_formats[i],&d));
        for(unsigned j=0;j<32;++j)output[j]=0xdead0000u+j;
        LCHECK(d9wg_process_vertices(&s,input,sizeof(input),0,s.stride,0,&d,output,d.stride,0,1,identity,projection,&vp,&light,1,0));
        for(unsigned j=d.stride/4;j<32;++j)LCHECK(output[j]==0xdead0000u+j);
    }
    D9WGProcessLayout s,d;uint32_t input[3]={0},output[3]={0};
    LCHECK(d9wg_process_fvf(7,2,&s));LCHECK(d9wg_process_fvf(9,2,&d));LCHECK(!d.legacy_xyz_output);
    LCHECK(!d9wg_process_vertices(&s,input,12,0,12,0,&d,output,12,0,1,identity,identity,&vp,&light,1,0));
    LCHECK(!d9wg_process_legacy_output(6,&d));
    return 0;
}
#undef LCHECK
`;
if(process.env.D9WG_LEGACY_OUTPUT_HEADER_OUT) {
    fs.writeFileSync(process.env.D9WG_LEGACY_OUTPUT_HEADER_OUT, "#define LEGACY_ERROR(r,i,g,w) ((void)0)\n" + code);
    console.log("PASS generated portable legacy output cases: " + rows.length);
} else {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-legacy-output-"));
    try {
        const input = path.join(directory, "test.c"), binary = path.join(directory, "test");
        fs.writeFileSync(input, '#include <stdio.h>\n#define LEGACY_ERROR(r,i,g,w) fprintf(stderr,"native row %u word %u: %08x != %08x\\n",r,i,g,w)\n' + code + '\nint main(void){int e=legacy_output_cases();if(e){fprintf(stderr,"legacy output mismatch header line %d\\n",e);return 1;}puts("PASS native D3D7 legacy output: ' + rows.length + ' exact buffers; bounded spill, attributes, offsets, DONOTCOPYDATA and clipping");return 0;}\n');
        const sources = ["clip", "blend", "vertex", "fvf", "math", "lighting", "tnl_math"].map(n => n === "vertex" && process.env.D9WG_VERTEX_SOURCE || path.join(root, "runtime/d9wg_" + n + ".c"));
        const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root, "-I", path.join(root, "runtime"), input, ...sources, "-o", binary], { encoding: "utf8" });
        assert.equal(built.status, 0, built.stderr.slice(0,3000) || String(built.error));
        const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0,1500) || String(run.error));process.stdout.write(run.stdout);
    } finally { fs.rmSync(directory,{recursive:true,force:true}); }
}
