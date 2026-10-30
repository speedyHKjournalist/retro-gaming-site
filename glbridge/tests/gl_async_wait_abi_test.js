#!/usr/bin/env node
// Exercise the actual C wait loop and arena allocator with a deterministic
// clock and changing heartbeat, without sleeping six seconds per case.
"use strict";
const fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const assert = require("node:assert/strict"), { execFileSync } = require("node:child_process");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const between = (begin, end) => { const at = source.indexOf(begin); assert.ok(at >= 0); return source.slice(at, source.indexOf(end, at)); };
const allocator = between("static int reserve_pci_record(", "static int emit_pci_record(");
const wait = between("static BOOL wait_for_host_status(", "static int emit_read_pixels(");
const c = `
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gl_protocol.h"
typedef int BOOL;
typedef uint32_t DWORD;
#define TRUE 1
#define FALSE 0
#define GL_OUT_OF_MEMORY 0x505
#define GL_INVALID_OPERATION 0x502
#define V86GL_DIAG_ENABLED 0
#define CopyMemory memcpy
#define MemoryBarrier() ((void)0)
#define V86GL_DIAG_RECORD(a,b) ((void)0)
typedef struct { uint32_t words[8]; } V86GLDMADesc;
static union { uint32_t align; uint8_t bytes[1024]; } arena;
static uint8_t* g_dma_buffer = arena.bytes;
static uint32_t g_dma_capacity = sizeof(arena.bytes), g_dma_size, g_dma_command_count, g_frame_id;
static uint32_t tick, elapsed, mode, status, last_error, flushes;
static uint32_t* beat(void) { return (uint32_t*)(g_dma_buffer + g_dma_capacity - V86GL_HEARTBEAT_BYTES); }
static DWORD GetTickCount(void) { return tick; }
static void Sleep(unsigned int ms) {
    assert(ms == 1); tick += 500; elapsed += 500;
    if (mode == 1) { ++*beat(); if (elapsed >= 10000) status = 1; }
    if (mode == 2 && elapsed == 2000) ++*beat();
    if (mode == 3) status = 2;
    assert(elapsed <= 20000);
}
static void v86gl_trace(const char* format, ...) { (void)format; }
static void v86gl_error(const char* format, ...) { (void)format; }
static BOOL v86gl_trace_call_allowed(void) { return g_frame_id != 0; }
static void v86gl_set_error(uint32_t value) { last_error = value; }
static BOOL open_v86gl(void) { return TRUE; }
static int emit_pci_batch(BOOL present) { (void)present; ++flushes; g_dma_size = 0; return 1; }
static void write_u32le(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }
${allocator}
${wait}
static void reset(unsigned int which, uint32_t start) {
    mode = which; tick = start; elapsed = status = 0; *beat() = 0;
}
int main(void) {
    uint8_t* out;
    reset(0, 0); status = 1;
    assert(wait_for_host_status((uint8_t*)&status, "ready") && elapsed == 0);
    reset(3, 0); assert(!wait_for_host_status((uint8_t*)&status, "failed") && elapsed == 500);
    reset(0, 0); assert(!wait_for_host_status((uint8_t*)&status, "silent") && elapsed == V86GL_ASYNC_SILENCE_MS);
    reset(1, 0); assert(wait_for_host_status((uint8_t*)&status, "busy") && elapsed == 10000);
    reset(2, 0); assert(!wait_for_host_status((uint8_t*)&status, "stopped") && elapsed == 2000 + V86GL_ASYNC_SILENCE_MS);
    reset(0, UINT32_MAX - 1000); assert(!wait_for_host_status((uint8_t*)&status, "wrap") && elapsed == V86GL_ASYNC_SILENCE_MS);
    *beat() = 0xCAFEBABE; g_dma_size = 0;
    /* A record exactly fills the command area, leaving the heartbeat intact. */
    assert(reserve_pci_record(1, NULL, sizeof(arena.bytes)-sizeof(V86GLDMADesc)-V86GL_HEARTBEAT_BYTES-4, &out));
    memset(out, 0x5A, sizeof(arena.bytes)-sizeof(V86GLDMADesc)-V86GL_HEARTBEAT_BYTES-4);
    assert(*beat() == 0xCAFEBABE);
    assert(reserve_pci_record(1, NULL, 1, &out) && flushes == 1 && *beat() == 0xCAFEBABE);
    g_dma_size = 0; last_error = 0;
    assert(!reserve_pci_record(1, NULL, sizeof(arena.bytes)-sizeof(V86GLDMADesc)-V86GL_HEARTBEAT_BYTES-3, &out));
    assert(last_error == GL_OUT_OF_MEMORY && *beat() == 0xCAFEBABE);
    g_dma_capacity = 1; last_error = 0;
    assert(!reserve_pci_record(1, NULL, 1, &out) && last_error == GL_OUT_OF_MEMORY);
    puts("gl_async_wait_abi_test: ready/failure, silence/progress, clock wrap and arena-tail bounds passed");
}
`;
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-async-wait-"));
try {
    const file = path.join(dir, "test.c"), binary = path.join(dir, "test"); fs.writeFileSync(file, c);
    execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", path.join(__dirname, "../openglproxy"), file, "-o", binary]);
    process.stdout.write(execFileSync(binary));
} finally { fs.rmSync(dir, { recursive: true, force: true }); }
