#!/usr/bin/env node
"use strict";

// Compile the actual buffered writer/checkpoint functions against a small
// Win32 I/O shim. This exercises batching and short writes without an XP VM.
const fs = require("node:fs"), path = require("node:path");
const os = require("node:os"), assert = require("node:assert/strict");
const { execFileSync } = require("node:child_process");
const header = fs.readFileSync(path.join(__dirname, "../diagnostic_trace.h"), "utf8");
const between = (start, end) => header.slice(header.indexOf(start), header.indexOf(end));
const helpers = between("/* Opt-in:", "/* OutputDebugString");
const write = between("static void v86wg_diagnostic_write(", "static void v86wg_diagnostic_memory(");
const flush = between("static void v86wg_diagnostic_flush(", "/* Unlike v86wg_diagnostic_hresult");
assert.ok(helpers.includes("v86wg_diagnostic_drain"));
assert.ok(write.includes("WriteFile"));
assert.ok(flush.includes("v86wg_diagnostic_checkpoint"));
const source = `
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef unsigned long DWORD;
typedef long LONG;
typedef int BOOL;
typedef int CRITICAL_SECTION;
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE -1
#define V86WG_DIAGNOSTIC_BUFFER_BYTES 65536u
#define CopyMemory memcpy
#define wsprintfA sprintf
#define wvsprintfA vsprintf
#define lstrlenA strlen
static int g_v86wg_trace_file = 1;
static LONG g_v86wg_trace_in_exception, g_v86wg_trace_sequence;
static DWORD tick, last_error, writes, flushes, short_limit;
static int fail_write, lock_busy;
static char disk[4 * 1024 * 1024];
static size_t disk_used;
static DWORD GetTickCount(void) { return tick; }
static DWORD GetLastError(void) { return last_error; }
static void SetLastError(DWORD value) { last_error = value; }
static DWORD GetCurrentProcessId(void) { return 1; }
static DWORD GetCurrentThreadId(void) { return 2; }
static LONG InterlockedIncrement(LONG *value) { return ++*value; }
static void EnterCriticalSection(CRITICAL_SECTION *lock) { ++*lock; }
static BOOL TryEnterCriticalSection(CRITICAL_SECTION *lock) {
    if (lock_busy) return FALSE;
    ++*lock; return TRUE;
}
static void LeaveCriticalSection(CRITICAL_SECTION *lock) { --*lock; }
static BOOL WriteFile(int file, const void *data, DWORD size, DWORD *written, void *unused) {
    (void)file; (void)unused;
    ++writes; last_error = 999;
    if (fail_write) { *written = 0; return FALSE; }
    if (short_limit && size > short_limit) size = short_limit;
    assert(disk_used + size < sizeof(disk));
    memcpy(disk + disk_used, data, size); disk_used += size;
    *written = size; return TRUE;
}
static BOOL FlushFileBuffers(int file) { (void)file; ++flushes; last_error = 998; return TRUE; }
${helpers}
${write}
${flush}
int main(void) {
    DWORD initial_writes, initial_flushes, tail;
    last_error = 42;
    for (int i = 0; i < 100; ++i) v86wg_diagnostic_write("CALL %d", i);
    assert(writes == 0 && g_v86wg_trace_buffer_used > 0 && last_error == 42);
    tick = 999; v86wg_diagnostic_checkpoint();
    assert(writes == 0 && flushes == 0);
    tick = 1000; v86wg_diagnostic_checkpoint();
    assert(writes == 1 && flushes == 1 && !g_v86wg_trace_buffer_used);
    assert(strstr(disk, "CALL 0\\r\\n") && strstr(disk, "CALL 99\\r\\n"));
    initial_writes = writes;
    for (int i = 0; i < 10000; ++i) v86wg_diagnostic_write("BATCHED %d", i);
    v86wg_diagnostic_flush();
    assert(writes - initial_writes < 20 && !g_v86wg_trace_buffer_used);
    // Short successful writes must preserve the entire ordered tail.
    v86wg_diagnostic_write("short-write-complete");
    short_limit = 7; v86wg_diagnostic_flush(); short_limit = 0;
    assert(strstr(disk, "short-write-complete\\r\\n"));
    v86wg_diagnostic_write("retry-after-failure"); tail = g_v86wg_trace_buffer_used;
    fail_write = 1; v86wg_diagnostic_flush();
    assert(g_v86wg_trace_buffer_used == tail);
    fail_write = 0; v86wg_diagnostic_flush();
    assert(!g_v86wg_trace_buffer_used && strstr(disk, "retry-after-failure\\r\\n"));
    // The exception fallback must not wait for a faulted writer's lock.
    g_v86wg_trace_in_exception = 1; lock_busy = 1;
    initial_writes = writes;
    v86wg_diagnostic_write("EXCEPTION emergency"); v86wg_diagnostic_flush();
    assert(writes == initial_writes + 1 && strstr(disk, "EXCEPTION emergency"));
    g_v86wg_trace_in_exception = 0; lock_busy = 0;
    // Explicit zero restores a durable checkpoint every frame.
    initial_flushes = flushes; g_v86wg_trace_flush_ms = 0;
    v86wg_diagnostic_write("synchronous"); v86wg_diagnostic_checkpoint();
    assert(flushes == initial_flushes + 1 && !g_v86wg_trace_buffer_used);
    assert(g_v86wg_trace_lock == 0 && last_error == 42);
    puts("gl_diagnostic_buffer_test: batching, checkpoint, short-write, failure, exception PASS");
}
`;
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "gl-trace-buffer-"));
const file = path.join(dir, "test.c"), binary = path.join(dir, "test");
fs.writeFileSync(file, source);
execFileSync(process.env.CC_HOST || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", file, "-o", binary]);
process.stdout.write(execFileSync(binary));
