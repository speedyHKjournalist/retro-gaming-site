"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const { spawnSync } = require("node:child_process"), { createHash } = require("node:crypto"), { testPath } = require("./host_paths.js");
const root = path.resolve(__dirname, "..");
const scalar = value => String(value).match(/[.eE]/) ? value + "f" : value + ".0f";
const vector = value => "{" + value.map(scalar).join(",") + "}";
const material = value => "{" + ["diffuse", "ambient", "specular", "emissive"].map(key => "." + key + "=" + vector(value[key])).join(",") + ",.power=" + scalar(value.power) + "}";
const light = value => "{" + ["index", "type"].map(key => "." + key + "=" + value[key] + "u").concat(
    ["diffuse", "specular", "ambient", "position", "direction", "attenuation"].map(key => "." + key + "=" + vector(value[key])),
    ["range", "falloff", "theta", "phi"].map(key => "." + key + "=" + scalar(value[key]))).join(",") + "}";
function buildFixture(options = {}) {
    const data = fs.readFileSync(testPath(options.caseFile || "fixtures/d3d_software_lighting_cases.json")), input = JSON.parse(data);
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-lighting-"));
    try {
        const c = path.join(directory, "lighting.c"), binary = path.join(directory, "lighting");
        const cases = input.cases.map((value, index) => `
        {
            const D9WGSetMaterial material=${material(value.material)};
            const D9WGTNLLightingFlags flags={${Object.entries(value.flags).map(([key, n]) => "." + key + "=" + n + "u").join(",")}};
            const float view[16]=${vector(value.view)},world[16]=${vector(value.world)},color0[4]=${vector(value.color0)},color1[4]=${vector(value.color1)};
            const float normal[3]=${vector(value.hasNormal ? value.normal : [0, 0, 0])};
            (void)normal;
            D9WGTNLLighting state;float world_view[16],eye_normal[3];
            d9wg_multiply_matrix(world,view,world_view);
            d9wg_lighting_init(&state,&material,${value.ambient}u,&flags);
            d9wg_lighting_geometry(&state,world_view);d9wg_lighting_normal(&state,${value.hasNormal ? "normal" : "NULL"},eye_normal);
            ${value.lights.map((l, i) => `const D9WGSetLight light${i}=${light(l)};if(!d9wg_lighting_add(&state,&light${i},view))return 2;`).join("\n")}
            printf("${index ? "," : ""}{\\"name\\":\\"${value.name}\\",\\"vertices\\":[");
            ${value.positions.map((position, at) => `{
                const float position[4]={${position.map(scalar).join(",")},1.0f};float eye[4],diffuse[4],specular[4];
                d9wg_transform_vector4(world_view,position,eye);
                d9wg_lighting_shade(&state,eye,eye_normal,color0,color1,${value.colorMask}u,diffuse,specular);
                printf("${at ? "," : ""}{\\"position\\":[%.9g,%.9g,%.9g],\\"diffuse\\":[%.9g,%.9g,%.9g,%.9g],\\"specular\\":[%.9g,%.9g,%.9g,%.9g]}",
                    eye[0],eye[1],eye[2],diffuse[0],diffuse[1],diffuse[2],diffuse[3],specular[0],specular[1],specular[2],specular[3]);
            }`).join("\n")}
            printf("]");
            {
                struct {float position[3],normal[3];uint32_t color0,color1;float texture[2][4],weights[4];uint32_t indices;} source[3];
                struct VertexOutput {float position[4];uint32_t color0,color1;};
                union { uint32_t words[114];uint8_t bytes[456]; } destination;
                D9WGVertexElement elements[]={{0,0,2,0,0,0},${value.hasNormal ? "{0,12,2,0,3,0}," : ""}${value.colorMask & 1 ? "{0,24,4,0,10,0}," : ""}${value.colorMask & 2 ? "{0,28,4,0,10,1}," : ""}${value.texture ? value.texture.dimensions.map((dim,i) => dim ? "{0,"+(32+i*16)+","+(dim-1)+",0,5,"+i+"}," : "").join("") : ""}${value.blend && value.blend.mode !== 256 ? "{0,64,"+(value.blend.weightType === undefined ? value.blend.mode-1 : value.blend.weightType)+",0,1,0}," : ""}${value.blend && value.blend.indexed ? "{0,80,"+value.blend.indexType+",0,2,0}," : ""}};
                D9WGProcessLayout input_layout,output_layout;
                const float positions[3][3]= {${value.positions.map(vector).join(",")}};
                for(unsigned v=0;v<3;v++) {
                    for(unsigned i=0;i<3;i++) {source[v].position[i]=positions[v][i];source[v].normal[i]=normal[i];}
                    source[v].color0=d9wg_pack_color(color0);source[v].color1=d9wg_pack_color(color1);
                    const float texture[2][4]=${value.texture ? "{"+value.texture.coordinates.map(vector).join(",")+"}" : "{{0},{0}}"};
                    for(unsigned t=0;t<2;t++)for(unsigned c=0;c<4;c++)source[v].texture[t][c]=texture[t][c];
                    const float weights[3][4]=${value.blend ? "{"+value.blend.weights.map(vector).join(",")+"}" : "{{0},{0},{0}}"};
                    const uint32_t indices[3]=${value.blend ? "{"+value.blend.indices.map(a => (value.blend.indexType === 4 ? ((a[3]<<24)|(a[0]<<16)|(a[1]<<8)|a[2])>>>0 : ((a[3]<<24)|(a[2]<<16)|(a[1]<<8)|a[0])>>>0)+"u").join(",")+"}" : "{0,0,0}"};
                    for(unsigned c=0;c<4;c++){source[v].weights[c]=weights[v][c];}source[v].indices=indices[v];
                    ${value.blend && value.blend.indexFloats ? `const float index_float[3]=${vector(value.blend.indexFloats)};__builtin_memcpy(&source[v].indices,&index_float[v],4);` : ""}
                    ${value.blend && value.blend.weightColors ? `const uint32_t weight_color[3]={${value.blend.weightColors.map(a => (value.blend.weightType === 4 ? ((a[3]<<24)|(a[0]<<16)|(a[1]<<8)|a[2])>>>0 : ((a[3]<<24)|(a[2]<<16)|(a[1]<<8)|a[0])>>>0)+"u").join(",")}};__builtin_memcpy(&source[v].weights[0],&weight_color[v],4);` : ""}
                }
                D9WGProcessViewport viewport={0,0,64,64,0,1};
                D9WGVertexState vertex_state={${value.fog ? [".fog_mode="+value.fog.mode,".range_fog="+value.fog.range,".fog_start="+scalar(value.fog.start),".fog_end="+scalar(value.fog.end),".fog_density="+scalar(value.fog.density)].join(",") : "0"}};
                if(!d9wg_process_layout(elements,sizeof(elements)/sizeof(elements[0]),&input_layout) ||
                        !d9wg_process_fvf(9,${value.texture ? "0xaaaa08c4u" : "0xc4u"},&output_layout)) return 3;
                for(unsigned word=0;word<114;word++)destination.words[word]=0;
                ${value.texture ? `for(unsigned v=0;v<3;v++)for(unsigned t=0;t<8;t++) {float one=1;__builtin_memcpy(destination.bytes+v*output_layout.stride+24+t*16+12,&one,4);}` : ""}
                ${value.texture ? `const float texture_matrix[16]=${vector(value.texture.matrix)};
                if(!d9wg_vertex_texture_state(&vertex_state,0,${value.texture.index}u,${value.texture.flags}u,texture_matrix))return 4;` : ""}
                ${value.blend ? `float (*worlds)[16]=malloc(256u*64u);uint32_t blend_bytes=d9wg_blend_bytes(${value.blend.mode}u,${value.blend.indexed}u,256);
                D9WGVertexBlend *blend=malloc(blend_bytes);if(!worlds || !blend)return 5;
                for(unsigned m=0;m<256;m++)for(unsigned c=0;c<16;c++)worlds[m][c]=(c%5)==0 ? 1.0f : 0.0f;
                ${value.blend.worlds.map((world,i) => `const float world${i}[16]=${vector(world.matrix)};for(unsigned c=0;c<16;c++)worlds[${world.index}][c]=world${i}[c];`).join("\n")}
                const float projection[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
                if(!d9wg_blend_init(blend,blend_bytes,${value.blend.mode}u,${value.blend.indexed}u,worlds,256,0,view,projection))return 6;
                vertex_state.blend=blend;` : ""}
                if(
                        !d9wg_process_vertices(&input_layout,source,sizeof(source),0,sizeof(source[0]),0,
                            &output_layout,destination.bytes,sizeof(destination),0,3,world_view,world_view,&viewport,&state,1,&vertex_state)) return 3;
                printf(",\\"processed\\":[");
                for(unsigned v=0;v<3;v++) {
                    const uint8_t *result=destination.bytes+v*output_layout.stride;
                    float result_position[4],d[4],s[4];uint32_t c0,c1;
                    for(unsigned k=0;k<4;k++)__builtin_memcpy(&result_position[k],result+k*4u,4u);
                    __builtin_memcpy(&c0,result+16,4);__builtin_memcpy(&c1,result+20,4);
                    d9wg_lighting_color(c0,d);d9wg_lighting_color(c1,s);
                    printf("%s{\\"position\\":[%.9g,%.9g,%.9g,%.9g],\\"diffuse\\":[%.9g,%.9g,%.9g,%.9g],\\"specular\\":[%.9g,%.9g,%.9g,%.9g]}",v ? "," : "",
                        result_position[0],result_position[1],result_position[2],result_position[3],
                        d[0],d[1],d[2],d[3],s[0],s[1],s[2],s[3]);
                }
                printf("]");
                ${value.texture ? `printf(",\\"textureCoordinates\\":[");
                for(unsigned v=0;v<3;v++) {printf("%s[",v ? "," : "");
                    for(unsigned t=0;t<8;t++) {float c[4];for(unsigned k=0;k<4;k++)__builtin_memcpy(&c[k],destination.bytes+v*output_layout.stride+24+t*16+k*4,4);
                        printf("%s[%.9g,%.9g,%.9g,%.9g]",t ? "," : "",c[0],c[1],c[2],c[3]);}
                    printf("]");}printf("]");` : ""}
                ${value.blend ? "free(blend);free(worlds);" : ""}
            }
            ${value.reference ? `{
                const float eye[3]=${vector(value.reference.position)};float diffuse[4],specular[4];
                d9wg_lighting_shade(&state,eye,eye_normal,color0,color1,${value.colorMask}u,diffuse,specular);
                printf(",\\"reference\\":{\\"diffuse\\":[%.9g,%.9g,%.9g,%.9g],\\"specular\\":[%.9g,%.9g,%.9g,%.9g]}",
                    diffuse[0],diffuse[1],diffuse[2],diffuse[3],specular[0],specular[1],specular[2],specular[3]);
            }` : ""}
            printf("}");
        }
`).join("\n");
        fs.writeFileSync(c, `#include <stdio.h>
#include <stdlib.h>
#include "runtime/d9wg_blend.h"
#include "runtime/d9wg_lighting.h"
#include "runtime/d9wg_math.h"
#include "runtime/d9wg_vertex.h"
int main(void){printf("[");${cases}printf("]\\n");return 0;}
`);
        const sources = ["runtime/d9wg_lighting.c", "runtime/d9wg_tnl_math.c", "runtime/d9wg_math.c", "runtime/d9wg_vertex.c", "runtime/d9wg_fvf.c", "runtime/d9wg_blend.c"];
        const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", root,
            c, ...sources.map(p => path.join(root, p)), "-o", binary], { encoding: "utf8" });
        assert.equal(built.status, 0, built.stderr.slice(0, 4000) || String(built.error));
        const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr.slice(0, 1200) || String(run.error));
        const output = JSON.parse(run.stdout);
        function check(actual, expected, label) {
            for (const key of ["diffuse", "specular"]) for (let i = 0; i < 4; i++)
                assert.ok(Math.abs(actual[key][i] - expected[key][i]) <= input.tolerance.cpuAbsolute,
                    label + " " + key + "[" + i + "]: " + actual[key][i] + " != " + expected[key][i]);
        }
        output.forEach((actual, index) => {
            const test = input.cases[index];assert.equal(actual.name, test.name);
            if (test.expected) for (const vertex of actual.vertices) check(vertex, test.expected, actual.name);
            if (test.reference) check(actual.reference, test.reference, actual.name + " reference");
        });
        return { caseSha256: createHash("sha256").update(data).digest("hex"),
            sourceSha256: Object.fromEntries(sources.map(p => [p, createHash("sha256").update(fs.readFileSync(path.join(root, p))).digest("hex")])), cases: output };
    } finally { fs.rmSync(directory, { recursive: true, force: true }); }
}
module.exports = { buildFixture };
