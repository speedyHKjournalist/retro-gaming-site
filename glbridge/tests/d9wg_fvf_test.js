"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process"), { createHash } = require("node:crypto");
const root = path.resolve(__dirname, ".."), fixture = fs.readFileSync(path.join(__dirname, "fixtures/d9wg_fvf_baseline.json"));
const baseline = JSON.parse(fixture), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-fvf-"));
const wine = JSON.parse(fs.readFileSync(path.join(__dirname, "fixtures/d9wg_fvf_wine_reference.json"), "utf8"));
const frozen = (api, names) => baseline[api].join("\n").replace(new RegExp(names.join("|"), "g"), name => "old" + api + "_" + name);
function actual(file, name) {
    const source = fs.readFileSync(path.join(root, file), "utf8"), start = source.indexOf("static BOOL " + name + "(");
    let end = source.indexOf("{", start) + 1, depth = 1;
    assert.ok(start > 0);
    for (; depth; end++) depth += (source[end] === "{") - (source[end] === "}");
    return source.slice(start, end);
}
try {
    // Frozen legacy C methods are independent of Git history in shallow CI.
    assert.equal(createHash("sha256").update(fixture).digest("hex"), "4088334849a5c4cdc5614a571aeb1c967d10c2a42fe4f70a0e15457d0ab4b3fa");
    const input = path.join(directory, "fvf.c"), binary = path.join(directory, "fvf");
    fs.writeFileSync(input, `
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "runtime/d9wg_fvf.h"
typedef uint32_t DWORD,UINT;typedef int BOOL;typedef uint8_t BYTE;
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define D3DFVF_RESERVED0 1u
#define D3DFVF_XYZ 2u
#define D3DFVF_XYZRHW 4u
#define D3DFVF_XYZB1 6u
#define D3DFVF_XYZB4 12u
#define D3DFVF_XYZB5 14u
#define D3DFVF_XYZW 0x4002u
#define D3DFVF_LASTBETA_UBYTE4 0x1000u
#define D3DFVF_LASTBETA_D3DCOLOR 0x8000u
#define D3DFVF_NORMAL 0x10u
#define D3DFVF_PSIZE 0x20u
#define D3DFVF_DIFFUSE 0x40u
#define D3DFVF_SPECULAR 0x80u
#define D3DFVF_TEXCOUNT_MASK 0xf00u
#define D3DFVF_TEXCOUNT_SHIFT 8u
#define D3DDECLTYPE_FLOAT1 0
#define D3DDECLTYPE_FLOAT2 1
#define D3DDECLTYPE_FLOAT3 2
#define D3DDECLTYPE_FLOAT4 3
#define D3DDECLTYPE_D3DCOLOR 4
#define D3DDECLTYPE_UBYTE4 5
#define D3DDECLMETHOD_DEFAULT 0
#define D3DDECLUSAGE_POSITION 0
#define D3DDECLUSAGE_POSITIONT 9
#define D3DDECLUSAGE_BLENDWEIGHT 1
#define D3DDECLUSAGE_BLENDINDICES 2
#define D3DDECLUSAGE_NORMAL 3
#define D3DDECLUSAGE_PSIZE 4
#define D3DDECLUSAGE_COLOR 10
#define D3DDECLUSAGE_TEXCOORD 5
#define D3D9DECLTYPE_FLOAT1 0
#define D3D9DECLTYPE_FLOAT2 1
#define D3D9DECLTYPE_FLOAT3 2
#define D3D9DECLTYPE_FLOAT4 3
#define D3D9DECLTYPE_D3DCOLOR 4
#define D3D9DECLTYPE_UBYTE4 5
#define D3D9DECLMETHOD_DEFAULT 0
#define D3D9DECLUSAGE_POSITION 0
#define D3D9DECLUSAGE_POSITIONT 9
#define D3D9DECLUSAGE_BLENDWEIGHT 1
#define D3D9DECLUSAGE_BLENDINDICES 2
#define D3D9DECLUSAGE_NORMAL 3
#define D3D9DECLUSAGE_PSIZE 4
#define D3D9DECLUSAGE_COLOR 10
#define D3D9DECLUSAGE_TEXCOORD 5
#define D3DFVF_POSITION_MASK 0x400eu
${frozen("d3d9", ["fvf_position_is_blended", "fvf_has_blend_indices", "fvf_to_declaration"])}
#undef D3DFVF_POSITION_MASK
#define D3DFVF_POSITION_MASK 0x000eu
${frozen("d3d8", ["fvf_position_is_blended", "fvf_has_blend_indices", "fvf_to_elements"])}
#define DDWG_FVF_RESERVED0 1u
#define DDWG_FVF_XYZ 2u
#define DDWG_FVF_XYZRHW 4u
#define DDWG_FVF_XYZB1 6u
#define DDWG_FVF_XYZB4 12u
#define DDWG_FVF_POSITION_MASK_D3D7 14u
#define DDWG_FVF_NORMAL 0x10u
#define DDWG_FVF_RESERVED1_D3D7 0x20u
#define DDWG_FVF_DIFFUSE 0x40u
#define DDWG_FVF_SPECULAR 0x80u
#define DDWG_FVF_TEXCOUNT_MASK 0xf00u
#define DDWG_FVF_TEXCOUNT_SHIFT 8u
#define D3D7_MAX_VERTEX_ELEMENTS 24u
${frozen("d3d7", ["D3D7FVFLayout", "d3d7_decode_fvf"])}
#define D3DMAXDECLLENGTH 18u
${actual("d3d9proxy/d3d9_proxy.c", "fvf_to_declaration")}
${actual("d3d8proxy/d3d8_proxy.c", "fvf_to_elements")}
typedef D9WGFVFLayout D3D7FVFLayout;
${actual("ddrawproxy/d3d7_proxy.inc", "d3d7_decode_fvf")}
static unsigned compared[3],accepted[3],corrected;
/* Known native declaration facts from pinned Wine's Windows conformance
 * table override only these erroneous legacy type tags. Frozen functions
 * and their SHA remain unchanged for all other mechanics. */
static void native_d3d9_types(uint32_t fvf,D9WGVertexElement *elements,unsigned count) {
    uint32_t position=fvf&0x400eu;int changed=0;
    for(unsigned i=0;i<count;i++) {
        uint32_t type=elements[i].type;
        if(elements[i].usage==1 && position==8u && (fvf&0x8000u))type=4;
        if(elements[i].usage==2)type=(fvf&0x1000u)||(position==8u&&(fvf&0x8000u)) ? 5u : (fvf&0x8000u) ? 4u : 0u;
        changed|=elements[i].type!=type;elements[i].type=(uint8_t)type;
    }
    corrected+=(unsigned)changed;
}
static void compare(uint32_t fvf) {
    for(unsigned api=7;api<=9;api++) {
        D9WGVertexElement old[32],now[32];UINT old_count=0,now_count=0;int a,b;
        oldd3d7_D3D7FVFLayout old_layout;D9WGFVFLayout layout;
        memset(old,0xa5,sizeof(old));memset(now,0xa5,sizeof(now));
        if(api==9) { a=oldd3d9_fvf_to_declaration(fvf,old,&old_count);b=fvf_to_declaration(fvf,now,&now_count); }
        else if(api==8) { a=oldd3d8_fvf_to_elements(fvf,old,32,&old_count);b=fvf_to_elements(fvf,now,32,&now_count); }
        else { a=oldd3d7_d3d7_decode_fvf(fvf,&old_layout,old,&old_count);b=d3d7_decode_fvf(fvf,&layout,now,&now_count); }
        if(api==9 && a)native_d3d9_types(fvf,old,old_count);
        if(a!=b || (a && (old_count!=now_count || memcmp(old,now,old_count*sizeof(*old)) ||
                (api==7 && (sizeof(old_layout)!=sizeof(layout) || memcmp(&old_layout,&layout,sizeof(layout))))))) {
            fprintf(stderr,"FVF mismatch API %u token %08x old/new %d/%d elements %u/%u\\n",api,fvf,a,b,old_count,now_count);assert(0);
        }
        compared[api-7]++;accepted[api-7]+=(a!=0);
    }
}
int main(void) {
    const uint32_t high[]={0,0x55550000u,0xaaaa0000u,0xffff0000u};
    const uint32_t low[]={0x8d2u,0x180eu,0x8c4u};
    for(uint32_t bits=0;bits<65536u;bits++) {
        for(unsigned i=0;i<4;i++)compare(bits|high[i]);
        for(unsigned i=0;i<3;i++)compare((bits<<16u)|low[i]);
    }
    ${wine.cases.map(c => `{const D9WGVertexElement expected[]={${c.elements.map(e => "{"+[e.stream,e.offset,e.type,e.method,e.usage,e.usage_index].join(",")+"}").join(",")}};D9WGVertexElement actual[32];UINT count=0;assert(fvf_to_declaration(${c.fvf}u,actual,&count)&&count==${c.elements.length}&&!memcmp(actual,expected,sizeof(expected)));}`).join("\n")}
    uint32_t seed=0x31586a29u;
    for(unsigned i=0;i<100000;i++) { seed=seed*1664525u+1013904223u;compare(seed); }
    D9WGVertexElement e[32];D9WGFVFLayout layout;uint32_t n=0;
    assert(d9wg_fvf_decode(7,0x62,e,32,&n,&layout)&&n==2&&layout.stride==20&&layout.diffuse_offset==16);
    assert(d9wg_fvf_decode(8,0x62,e,32,&n,&layout)&&n==3&&e[1].usage==4&&e[1].offset==12);
    assert(d9wg_fvf_decode(9,0x4002,e,32,&n,&layout)&&n==1&&e[0].type==3&&e[0].usage==0&&layout.stride==16);
    assert(d9wg_fvf_decode(8,0x4002,e,32,&n,&layout)&&n==1&&e[0].type==2&&layout.stride==12);
    assert(!d9wg_fvf_decode(7,14,e,32,&n,&layout));
    assert(d9wg_fvf_decode(9,0x8006,e,32,&n,&layout)&&n==2&&e[1].type==4&&e[1].usage==2);
    assert(d9wg_fvf_decode(8,0x8006,e,32,&n,&layout)&&n==2&&e[1].type==0&&e[1].usage==1);
    memset(e,0xa5,sizeof(e));D9WGVertexElement canary=e[1];
    assert(!d9wg_fvf_decode(9,0x42,e,1,&n,&layout)&&!memcmp(&canary,&e[1],sizeof(canary)));
    assert(!d9wg_fvf_decode(9,2,NULL,32,&n,&layout));
    assert(!d9wg_fvf_decode(9,2,e,32,NULL,&layout));
    assert(!d9wg_fvf_decode(6,2,e,32,&n,&layout));
    printf("PASS actual three FVF frontends: %u frozen C comparisons; accepted D3D7/8/9=%u/%u/%u; API-specific layouts and bounded output passed\\n",
        compared[0]+compared[1]+compared[2],accepted[0],accepted[1],accepted[2]);printf("Native D3D9 declarations: ${wine.cases.length} Wine cases; corrected legacy type tags for %u input patterns\\n",corrected);return 0;
}
`);
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", root,
        input, path.join(root, "runtime/d9wg_fvf.c"), "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 4000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1200) || String(run.error));
    process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
