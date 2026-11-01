"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { spawnSync, execFileSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-runtime-"));
function definition(source, signature) {
    const start = source.indexOf(signature);assert(start >= 0, signature);
    let at = source.indexOf("{", start), depth = 1;
    for (at++; depth; at++) { if (source[at] === "{") depth++;else if (source[at] === "}") depth--; }
    return source.slice(start, at);
}
try {
    for (const api of ["d3d9", "d3d8", "ddraw"]) {
        const relative = api + "proxy/" + api + "_proxy.c";
        const source = process.env.D9WG_UPLOAD_BASELINE ? execFileSync("git", ["show", "f7f1199:glbridge/" + relative], { cwd: path.dirname(root), encoding: "utf8" })
            : fs.readFileSync(path.join(root, relative), "utf8");
        // DDraw's common emitter is new; its former two inline VB paths are
        // covered by integration/build checks and both now call this function.
        if (process.env.D9WG_UPLOAD_BASELINE && api === "ddraw") continue;
        const callback = source.includes("static int emit_buffer_chunk_locked(")
            ? definition(source, "static int emit_buffer_chunk_locked(") : "";
        const bridge = definition(source, "static BOOL emit_buffer_update(");
        const code = `
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "d3d9proxy/d3d9_protocol.h"
#include "runtime/d9wg_runtime.h"
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define D3DLOCK_DISCARD 0x2000u
#define DDLOCK_DISCARDCONTENTS 0x2000u
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory memcpy
#define CAPACITY (16u*1024u*1024u-4u*1024u*1024u-36u)
#define TOTAL (17u*1024u*1024u+13u)
static uint8_t arena[CAPACITY], input[TOTAL], output[TOTAL];
static uint32_t used=sizeof(D9WGBatchHeader), uploaded, records, flushes, expected_flags=0x3000u;
static int g_transport_lock, open_ok=1, fail_record;
static unsigned reserve_attempts;
static void EnterCriticalSection(int *lock){assert(!*lock);*lock=1;}
static void LeaveCriticalSection(int *lock){assert(*lock==1);*lock=0;}
static BOOL open_transport_locked(void){assert(g_transport_lock==1);return open_ok;}
static uint8_t *batch_base(void){return arena;}
static uint32_t batch_capacity(void){return CAPACITY;}
static void flush(void){
 for(uint32_t at=sizeof(D9WGBatchHeader);at<used;){
  D9WGCommandHeader *command=(D9WGCommandHeader*)(arena+at);
  D9WGUpdateBuffer *update=(D9WGUpdateBuffer*)(command+1);
  assert(command->opcode==D9WG_OP_UPDATE_BUFFER&&!(command->size&7u));
  assert(update->resource_handle==0x12345678u&&update->destination_offset==9u+uploaded);
  assert(update->data_offset==(uint32_t)((uint8_t*)(update+1)-arena));
  assert(update->byte_count<=TOTAL-uploaded&&update->data_offset+update->byte_count<=used);
  assert(update->lock_flags==(records?expected_flags&~0x2000u:expected_flags));
  memcpy(output+uploaded,arena+update->data_offset,update->byte_count);
  uploaded+=update->byte_count;records++;at+=command->size;
 }
 used=sizeof(D9WGBatchHeader);flushes++;
}
static BOOL reserve_command_locked(uint16_t opcode,uint32_t payload_bytes,uint32_t extra_bytes,
 ${api === "ddraw" ? "" : "D9WGCommandHeader **command_out,"}uint8_t **payload,uint8_t **extra){
 assert(g_transport_lock==1);reserve_attempts++;if(fail_record&&reserve_attempts==(unsigned)fail_record)return FALSE;
 uint32_t size=D9WG_ALIGN8(sizeof(D9WGCommandHeader)+payload_bytes+extra_bytes);
 if(size>CAPACITY-sizeof(D9WGBatchHeader))return FALSE;
 if(used+size>CAPACITY)flush();
 D9WGCommandHeader *command=(D9WGCommandHeader*)(arena+used);memset(command,0,size);
 command->opcode=opcode;command->size=size;
 ${api === "ddraw" ? "" : "if(command_out)*command_out=command;"}
 *payload=(uint8_t*)(command+1);*extra=*payload+payload_bytes;used+=size;return TRUE;
}
${callback}
${bridge}
static unsigned count;
static int probe(void *context,uint32_t offset,const uint8_t *data,uint32_t bytes,uint32_t flags){
 (void)context;(void)offset;(void)data;(void)bytes;(void)flags;count++;return count<2;
}
int main(void){
 (void)open_transport_locked;(void)batch_capacity;
 assert(d9wg_retry_ready(0,0,0));assert(!d9wg_retry_ready(999,0,1));assert(d9wg_retry_ready(1000,0,1));
 assert(!d9wg_retry_ready(743,0xffffff00u,1));assert(d9wg_retry_ready(744,0xffffff00u,1));
 assert(!d9wg_buffer_upload_limit(16,32,16,24));
 assert(!d9wg_buffer_upload_limit(0xffffffffu,0,0xffffffffu,1));
 for(uint32_t capacity=0;capacity<1024;capacity++){
  uint32_t limit=d9wg_buffer_upload_limit(capacity,40,16,24);
  if(limit){assert(40u+D9WG_ALIGN8(40u+limit)<=capacity);assert(40u+D9WG_ALIGN8(41u+limit)>capacity);}
 }
 for(uint32_t i=0;i<TOTAL;i++)input[i]=(uint8_t)(i*73u+(i>>16));
 assert(emit_buffer_update(0x12345678u,9,input,TOTAL,expected_flags));flush();
 assert(uploaded==TOTAL&&records==2&&flushes==2&&!memcmp(output,input,TOTAL)&&!g_transport_lock);
 uploaded=records=flushes=0;reserve_attempts=0;fail_record=2;
 assert(!emit_buffer_update(0x12345678u,9,input,TOTAL,expected_flags));flush();
 assert(records==1&&uploaded>0&&uploaded<TOTAL&&!memcmp(output,input,uploaded)&&!g_transport_lock);
 fail_record=0;
 unsigned old=records;open_ok=0;assert(!emit_buffer_update(0x12345678u,9,input,1,0));assert(records==old&&!g_transport_lock);open_ok=1;
 assert(!emit_buffer_update(0x12345678u,0xffffffffu,input,2,0));assert(!g_transport_lock);
 assert(!emit_buffer_update(0x12345678u,0,NULL,2,0));
 assert(!d9wg_upload_buffer(0,0,input,32,0,0x2000,8,probe));assert(count==2);
 printf("PASS %s actual C upload: %u bytes, %u framed bands, one DISCARD, ordered bytes, wrap-safe retry and failure bounds\\n","${api}",TOTAL,2u);
 return 0;
}
`;
        const input = path.join(directory, api + ".c"), binary = path.join(directory, api);
        fs.writeFileSync(input, code);
        const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", root,
            input, path.join(root, "runtime/d9wg_runtime.c"), "-o", binary], { encoding: "utf8" });
        assert.equal(built.status, 0, built.stderr || String(built.error));
        const run = spawnSync(binary, [], { encoding: "utf8" });
        assert.equal(run.status, 0, run.stderr || String(run.error));process.stdout.write(run.stdout);
    }
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
