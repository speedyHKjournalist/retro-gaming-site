"use strict";
// Compile the shipping exports and core parser with real SDK constants.
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path"), os = require("node:os");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d8-validation-"));
function extract(source, name) {
    const match = new RegExp("^[A-Za-z_][^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{", "m").exec(source);
    assert(match, name);
    let end = source.indexOf("{", match.index) + 1, depth = 1;
    for(; depth; end++) { if(source[end] === "{") depth++; if(source[end] === "}") depth--; }
    return source.slice(match.index, end).replace(/__attribute__\s*\(\(__stdcall__\)\)/g, "");
}
function run(command, args) {
    const result = spawnSync(command, args, {encoding: "utf8", maxBuffer: 16 << 20});
    assert.equal(result.status, 0, result.stderr + result.stdout + " signal=" + result.signal);
    return result.stdout;
}
try {
    const eight = fs.readFileSync(process.env.D8WG_VALIDATION_EXPORTS_SOURCE || root + "/d3d8proxy/d3d8_proxy.c", "utf8");
    const nine = fs.readFileSync(process.env.D8WG_VALIDATION_EXPORTS_CORE || root + "/d3d9proxy/d3d9_proxy.c", "utf8");
    const names = ["shader8_regtype_valid", "shader8_opcode_supported", "shader8_validate_body", "d9wg_d3d9_core_validate_shader_export8"];
    const exports = ["shader_validation_result", "ValidateVertexShader", "ValidatePixelShader"];
    fs.writeFileSync(dir + "/sdk.c", '#include <d3d9.h>\n#define TRACE(...)\n#define D9WG_D3D8_SHADER_TOKENS 8192u\n' + names.map(n => extract(nine, n).replace(/\bWINAPI\b/g, "")).join("\n"));
    const sdk = run(process.env.D9WG_CC || "i686-w64-mingw32-gcc", ["-E", "-P", dir + "/sdk.c"]);
    const enums = ["D3DSHADER_INSTRUCTION_OPCODE_TYPE", "D3DSHADER_PARAM_REGISTER_TYPE"].map(n => {
        const match = sdk.match(new RegExp("typedef enum _" + n + "\\s*\\{[\\s\\S]*?\\}\\s*" + n + "\\s*;"));
        assert(match); return match[0];
    }).join("\n");
    fs.writeFileSync(dir + "/test.c", `
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "${root}/runtime/d9wg_declaration8.h"
typedef uint32_t DWORD, UINT; typedef uint16_t WORD; typedef int BOOL, WINBOOL; typedef int32_t HRESULT; typedef size_t SIZE_T;
typedef struct { DWORD VertexShaderVersion, MaxVertexShaderConst, PixelShaderVersion; } D3DCAPS8;
#define WINAPI
#define FALSE 0
#define TRUE 1
#define FAILED(hr) ((hr) < 0)
#define E_FAIL ((HRESULT)0x80004005u)
#define E_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define S_OK 0
#define D8WG_TRACE_ERROR(v) (v)
#define D3DVS_VERSION(a,b) (0xfffe0000u | ((a)<<8) | (b))
#define D3DPS_VERSION(a,b) (0xffff0000u | ((a)<<8) | (b))
static unsigned allocations, live, fail_next, checks;
static void *GetProcessHeap(void) { return NULL; }
static void *HeapAlloc(void *heap, unsigned flags, size_t size) { (void)heap; assert(!flags && size); allocations++; if(fail_next) { fail_next=0; return NULL; } live++; return malloc(size); }
static int lstrlenA(const char *s) { return (int)strlen(s); }
#define CopyMemory memcpy
${enums}
${names.map(n => extract(sdk, n)).join("\n")}
${exports.map(n => extract(eight, n)).join("\n")}
#define CHECK(v) do { checks++; if(!(v)) { fprintf(stderr, "line %u: %s\\n", __LINE__, #v); return 1; } } while(0)
int main(void) {
    DWORD vs[] = {0xfffe0101, 9, 0xc0010000, 0x90e40000, 0xa0e40003, 0xffff};
    DWORD ps[] = {0xffff0101, 1, 0x800f0000, 0xa0e40001, 0xffff};
    DWORD decl[] = {0x20000000, 0x40030000, 0xffffffff};
    D3DCAPS8 caps = {0xfffe0101, 4, 0xffff0104};
    for(UINT pixel=0; pixel<2; pixel++) for(UINT version=0; version<8; version++)
    for(UINT supported=0; supported<8; supported++) for(UINT muted=0; muted<2; muted++) {
        DWORD *code = pixel ? ps : vs;
        code[0] = pixel ? D3DPS_VERSION(1,version) : D3DVS_VERSION(1,version);
        if(pixel) caps.PixelShaderVersion=D3DPS_VERSION(1,supported); else caps.VertexShaderVersion=D3DVS_VERSION(1,supported);
        HRESULT expected = version <= (pixel ? 4u : 1u) && version <= supported ? S_OK : E_FAIL;
        char *error=(void*)(uintptr_t)1;
        HRESULT hr=pixel ? ValidatePixelShader(code,&caps,!muted,&error) : ValidateVertexShader(code,decl,&caps,!muted,&error);
        CHECK(hr==expected && error && live==1);
        CHECK(!!error[0] == (expected==E_FAIL && !muted)); free(error); live--;
    }
    vs[0]=0xfffe0101; ps[0]=0xffff0101; caps.VertexShaderVersion=0xfffe0101;
    for(UINT muted=0; muted<2; muted++) {
        char *error=(void*)(uintptr_t)1;
        CHECK(ValidatePixelShader(NULL,NULL,!muted,&error)==E_FAIL && error==(void*)(uintptr_t)1);
        CHECK(ValidateVertexShader(NULL,NULL,NULL,!muted,&error)==E_FAIL && error && !!error[0]==!muted);
        free(error); live--;
    }
    for(UINT limit=0; limit<10; limit++) { caps.MaxVertexShaderConst=limit; CHECK(ValidateVertexShader(vs,decl,&caps,0,NULL)==(limit>=4 ? S_OK:E_FAIL)); }
    caps.MaxVertexShaderConst=4;
    decl[1]=0x40030003; CHECK(ValidateVertexShader(vs,decl,&caps,0,NULL)==E_FAIL);
    decl[1]=0x40030000; CHECK(ValidateVertexShader(vs,decl,&caps,0,NULL)==S_OK);
    /* Literal END values in DEF / declaration constants must not truncate. */
    DWORD def[]={0xfffe0101,81,0xa00f0000,0xffff,0xffffffff,0,0,1,0xc00f0000,0x90e40000,0xffff};
    DWORD literal_decl[]={0x82000000,0xffffffff,0xffff,0,0,0x20000000,0x40030000,0xffffffff};
    CHECK(ValidateVertexShader(def,literal_decl,&caps,0,NULL)==S_OK);
    DWORD matrix[]={0xfffe0101,20,0xc00f0000,0x90e40000,0xa0e40001,0xffff};
    CHECK(ValidateVertexShader(matrix,decl,&caps,0,NULL)==E_FAIL); caps.MaxVertexShaderConst=5;
    CHECK(ValidateVertexShader(matrix,decl,&caps,0,NULL)==S_OK);
    DWORD invalid[]={0xfffe0101,0x1234,0xffff}; CHECK(ValidateVertexShader(invalid,NULL,NULL,0,NULL)==E_FAIL);
    char *error=(void*)(uintptr_t)1; fail_next=1;
    CHECK(ValidateVertexShader(vs,NULL,NULL,1,&error)==E_OUTOFMEMORY && !error && !live);
    fail_next=1; CHECK(ValidatePixelShader(ps,NULL,1,&error)==E_OUTOFMEMORY && !error && !live);
    unsigned before=allocations; CHECK(ValidateVertexShader(vs,NULL,NULL,0,NULL)==S_OK && allocations==before);
    CHECK(!live); printf("PASS validation exports: %u checks, real core parser, SDK constants, heap ownership and allocation failure\\n", checks);
    return 0;
}
`);
    run(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", dir + "/test.c", "-o", dir + "/test"]);
    process.stdout.write(run(dir + "/test", []));
} finally { fs.rmSync(dir, {recursive: true, force: true}); }
