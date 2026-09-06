#!/usr/bin/env node
// Execute the production capability-query function with captured diagnostics.
// A successful '-PCF' query disables 3DMark05's filtered-depth path. Its
// expected negative answer must be informational, while actual unsupported
// formats and malformed uses still produce refusal diagnostics.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { spawnSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../d3d9proxy/d3d9_proxy.c"), "utf8");
const start = source.indexOf("static HRESULT WINAPI d3d_check_device_format(");
const end = source.indexOf("static HRESULT WINAPI d3d_check_multisample", start);
assert.ok(start >= 0 && end > start);
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "d3d9-pcf-"));
const input = path.join(directory, "probe.c");
const binary = path.join(directory, "probe");
fs.writeFileSync(input, `
#include <assert.h>
#include <stdint.h>
typedef int BOOL;
typedef void IDirect3D9;
typedef uint32_t UINT, DWORD, D3DDEVTYPE, D3DFORMAT, D3DRESOURCETYPE;
typedef int32_t HRESULT;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define SUCCEEDED(hr) ((hr) >= 0)
#define D3D_OK ((HRESULT)0)
#define D3DERR_NOTAVAILABLE ((HRESULT)0x8876086A)
#define D3DDEVTYPE_HAL 1
#define D3DRTYPE_TEXTURE 3
#define D3DRTYPE_VOLUMETEXTURE 4
#define D3DPOOL_DEFAULT 0
#define D3DUSAGE_SOFTWAREPROCESSING 0x10u
#define D3DUSAGE_AUTOGENMIPMAP 0x400u
#define D3DUSAGE_DMAP 0x4000u
#define D3DUSAGE_QUERY_LEGACYBUMPMAP 0x8000u
#define D3DUSAGE_QUERY_SRGBREAD 0x10000u
#define D3DUSAGE_QUERY_FILTER 0x20000u
#define D3DUSAGE_QUERY_SRGBWRITE 0x40000u
#define D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING 0x80000u
#define D3DUSAGE_QUERY_VERTEXTEXTURE 0x100000u
#define D3DUSAGE_QUERY_WRAPANDMIP 0x200000u
#define D9FOURCC_NO_PCF 0x4643502Du
static unsigned infos, warnings;
#define TRACE(...) ((void)0)
#define HOSTLOG_INFO(...) (++infos)
#define HOSTLOG_REFUSED(...) (++warnings)
// The query's shared creation predicate is external to the behavior under
// test. Accept the D32 depth texture queried next by 3DMark05, nothing else.
static BOOL texture_create_supported(D3DRESOURCETYPE type, DWORD usage,
        D3DFORMAT format, DWORD pool) {
    return type == D3DRTYPE_TEXTURE && usage == 2 && format == 71 && pool == 0;
}
${source.slice(start, end)}
int main(void) {
    HRESULT hr = d3d_check_device_format(0, 0, D3DDEVTYPE_HAL, 22, 0, 3, D9FOURCC_NO_PCF);
    assert(hr == D3DERR_NOTAVAILABLE);
    assert(infos == 1 && warnings == 0);
    hr = d3d_check_device_format(0, 0, D3DDEVTYPE_HAL, 22, 0x20002, 3, 71);
    assert(hr == D3D_OK && warnings == 0);
    // Unrelated unknown formats and non-probe combinations stay visible.
    assert(d3d_check_device_format(0, 0, 1, 22, 0, 3, 0x12345678) == D3DERR_NOTAVAILABLE);
    assert(d3d_check_device_format(0, 0, 1, 22, 2, 3, D9FOURCC_NO_PCF) == D3DERR_NOTAVAILABLE);
    assert(d3d_check_device_format(0, 0, 1, 22, 0, 5, D9FOURCC_NO_PCF) == D3DERR_NOTAVAILABLE);
    assert(d3d_check_device_format(0, 1, 1, 22, 0, 3, D9FOURCC_NO_PCF) == D3DERR_NOTAVAILABLE);
    assert(d3d_check_device_format(0, 0, 2, 22, 0, 3, D9FOURCC_NO_PCF) == D3DERR_NOTAVAILABLE);
    assert(infos == 1 && warnings == 5);
    return 0;
}
`);
try {
    const build = spawnSync(process.env.HOST_CC || "cc",
        ["-std=c99", "-Wall", "-Wextra", "-Werror", input, "-o", binary], { encoding: "utf8" });
    assert.equal(build.status, 0, build.stderr || String(build.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });
    assert.equal(run.status, 0, run.stderr || String(run.error));
    console.log("d3d9_negative_pcf_probe_test: expected negative probe has zero warnings; other refusals remain visible");
} finally {
    fs.rmSync(directory, { recursive: true, force: true });
}
