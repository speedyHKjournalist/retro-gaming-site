"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { createHash } = require("node:crypto");
const root = path.resolve(__dirname, ".."), data = fs.readFileSync(path.join(__dirname, "fixtures/d9wg_math_baseline.json"));
assert.equal(createHash("sha256").update(data).digest("hex"), "387f4f0c2570f00a5cac1409519d2b85084c2b79cef70450d0c3cfa7e70e5607");
const baseline = JSON.parse(data);
const extra = fs.readFileSync(path.join(__dirname, "fixtures/d9wg_d3d7_multiply_baseline.json"));
assert.equal(createHash("sha256").update(extra).digest("hex"), "c409cee9eb93eb3b5316559cb20e184ba72d47f242a50c8067acad10b7db6432");
baseline.groups.d3d7.d3d7_multiply_matrix = JSON.parse(extra).function;
function body(source, name) {
    const match = new RegExp("static (?:float|void|D3DCOLOR|DWORD) " + name + "\\([^;]*?\\)\\s*\\{", "s").exec(source);
    if (!match) return null;let end = source.indexOf("{", match.index) + 1, depth = 1;
    for (; depth; end++) depth += (source[end] === "{") - (source[end] === "}");
    return source.slice(match.index, end);
}
const files = { d3d9: "d3d9proxy/d3d9_proxy.c", d3d8: "d3d8proxy/d3d8_proxy.c", d3d7: "ddrawproxy/d3d7_proxy.inc" };
const names = { d3d9: ["d9_sqrt", "d9_transform_vector4", "d9_multiply_matrix", "d9_pack_color"],
    d3d8: ["float_sqrt", "transform_vector4", "multiply_matrix", "pack_color"],
    d3d7: ["d3d7_sqrt", "d3d7_transform_vector", "d3d7_pack_color", "d3d7_multiply_matrix"] };
function source() {
    let code = `
#include <stdint.h>
#include "runtime/d9wg_math.h"
#ifndef _WIN32
typedef uint32_t UINT,DWORD;typedef uint32_t D3DCOLOR;
#else
typedef DWORD D3DCOLOR;
#endif
typedef union D3DMATRIX {
    float m[4][4];
    struct { float _11,_12,_13,_14,_21,_22,_23,_24,_31,_32,_33,_34,_41,_42,_43,_44; };
} D3DMATRIX;
`;
    for (const api of Object.keys(files)) {
        const oldNames = Object.keys(baseline.groups[api]);
        code += Object.values(baseline.groups[api]).join("\n").replace(new RegExp(oldNames.join("|"), "g"), name => "old_" + api + "_" + name) + "\n";
        const candidate = fs.readFileSync(path.join(root, files[api]), "utf8");
        const aliases = { d9_pack_color: "d9wg_pack_color", pack_color: "d9wg_pack_color", d3d7_pack_color: "d9wg_pack_color",
            d9_sqrt: "d9wg_sqrt_positive", float_sqrt: "d9wg_sqrt_positive", d3d7_sqrt: "d9wg_sqrt_positive" };
        code += names[api].map(name => {
            const actual = body(candidate, name);if (actual) return actual;
            assert.ok(aliases[name], name);return "#define " + name + " " + aliases[name];
        }).join("\n") + "\n";
    }
    code += `
static uint32_t math_seed;
static uint32_t math_random(void) { math_seed=math_seed*1664525u+1013904223u;return math_seed; }
static float math_input(void) { return ((int32_t)(math_random()&65535u)-32768)*0.0009765625f; }
static int math_same(float left,float right) {
    union {float number;uint32_t bits;} a,b;a.number=left;b.number=right;
    return a.bits==b.bits || ((a.bits&0x7fffffffu)>0x7f800000u && (b.bits&0x7fffffffu)>0x7f800000u);
}
#define MATH_CHECK(condition) do { if(!(condition)) return __LINE__; } while(0)
static unsigned math_comparisons;
static int math_cases(unsigned iterations) {
    math_seed=0x9bc7a241u;math_comparisons=0;
    for(unsigned at=0;at<iterations;at++) {
        D3DMATRIX left,right,old_matrix,now_matrix;
        float input[4],old_vector[4],now_vector[4],rgba[4];
        for(unsigned row=0;row<4;row++) {
            input[row]=math_input();rgba[row]=((int32_t)(math_random()&4095u)-2048)*0.0009765625f;
            for(unsigned column=0;column<4;column++) { left.m[row][column]=math_input();right.m[row][column]=math_input(); }
        }
        union {uint32_t bits;float number;} value;value.bits=math_random()&0x7f7fffffu;
        for(unsigned api=7;api<=9;api++) {
            float old_root,now_root;uint32_t old_color,now_color;
            if(api==7) {
                old_root=old_d3d7_d3d7_sqrt(value.number);now_root=d3d7_sqrt(value.number);
                old_color=old_d3d7_d3d7_pack_color(rgba);now_color=d3d7_pack_color(rgba);
                old_d3d7_d3d7_transform_vector(&left,input,old_vector);d3d7_transform_vector(&left,input,now_vector);
                old_d3d7_d3d7_multiply_matrix(&left,&right,&old_matrix);d3d7_multiply_matrix(&left,&right,&now_matrix);
            } else if(api==8) {
                old_root=old_d3d8_float_sqrt(value.number);now_root=float_sqrt(value.number);
                old_color=old_d3d8_pack_color(rgba);now_color=pack_color(rgba);
                old_d3d8_transform_vector4(&left,input,old_vector);transform_vector4(&left,input,now_vector);
                old_d3d8_multiply_matrix(&left,&right,&old_matrix);multiply_matrix(&left,&right,&now_matrix);
            } else {
                old_root=old_d3d9_d9_sqrt(value.number);now_root=d9_sqrt(value.number);
                old_color=old_d3d9_d9_pack_color(rgba);now_color=d9_pack_color(rgba);
                old_d3d9_d9_transform_vector4(&left,input,old_vector);d9_transform_vector4(&left,input,now_vector);
                old_d3d9_d9_multiply_matrix(&left,&right,&old_matrix);d9_multiply_matrix(&left,&right,&now_matrix);
            }
            MATH_CHECK(math_same(old_root,now_root));MATH_CHECK(old_color==now_color);math_comparisons+=2;
            for(unsigned row=0;row<4;row++)MATH_CHECK(math_same(old_vector[row],now_vector[row]));
            math_comparisons++;
            {
                for(unsigned row=0;row<4;row++)for(unsigned column=0;column<4;column++)
                    MATH_CHECK(math_same(old_matrix.m[row][column],now_matrix.m[row][column]));
                math_comparisons++;
            }
        }
    }
    D3DMATRIX matrix,scale,result;float input[4]={1,2,3,1},output[4];
    for(unsigned row=0;row<4;row++)for(unsigned column=0;column<4;column++) {
        matrix.m[row][column]=row==column?1.0f:0.0f;scale.m[row][column]=row==column?(row==3?1.0f:(float)(row+2u)):0.0f;
    }
    matrix._41=3;matrix._42=4;matrix._43=5;
    d9wg_transform_vector4(&matrix,input,output);
    MATH_CHECK(output[0]==4&&output[1]==6&&output[2]==8&&output[3]==1);
    input[3]=0;d9wg_transform_vector4(&matrix,input,output);
    MATH_CHECK(output[0]==1&&output[1]==2&&output[2]==3&&output[3]==0);
    d9wg_multiply_matrix(&scale,&matrix,&result);
    MATH_CHECK(result._11==2&&result._22==3&&result._33==4&&result._41==3&&result._42==4&&result._43==5);
    unsigned char unaligned[sizeof(matrix)+1u],destination[sizeof(matrix)+3u];
    for(unsigned i=0;i<sizeof(matrix);i++)unaligned[i+1u]=((unsigned char*)&matrix)[i];
    d9wg_transform_vector4(unaligned+1,input,output);MATH_CHECK(output[0]==1&&output[1]==2&&output[2]==3);
    d9wg_multiply_matrix(&scale,unaligned+1,destination+3);
    for(unsigned i=0;i<sizeof(result);i++)MATH_CHECK(destination[i+3u]==((unsigned char*)&result)[i]);
    const float color[4]={0.5f,0.0f,1.0f,0.25f};MATH_CHECK(d9wg_pack_color(color)==0x408000ffu);
    const uint32_t edges[]={0,0x80000000u,0xbf800000u,0x7fc00000u,0x7f800000u,1u,0x7f7fffffu};
    for(unsigned i=0;i<sizeof(edges)/sizeof(*edges);i++) {
        union {uint32_t bits;float number;} value;value.bits=edges[i];
        float now=d9wg_sqrt_positive(value.number);
        MATH_CHECK(math_same(now,old_d3d9_d9_sqrt(value.number)));
        MATH_CHECK(math_same(now,old_d3d8_float_sqrt(value.number)));
        MATH_CHECK(math_same(now,old_d3d7_d3d7_sqrt(value.number)));
    }
    return 0;
}
#undef MATH_CHECK
`;
    return code;
}
module.exports = { source };
