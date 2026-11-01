"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { spawnSync } = require("node:child_process");
const { createHash } = require("node:crypto");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-transport-"));
const frozenText = fs.readFileSync(path.join(__dirname, "fixtures/d9wg_transport_baseline.json"), "utf8");
assert.equal(createHash("sha256").update(frozenText).digest("hex"), "9f447fb9c7790c492f07a5b32ead783423c2992af6b0706f31a510d1cef0966f");
const baseline = JSON.parse(frozenText);
try {
    for (const api of ["d3d9", "d3d8", "ddraw"]) {
        // A fixed pre-extraction oracle, never updated from the candidate.
        const legacy = baseline.frontends[api].methods.join("\n");
        const input = path.join(directory, api + ".c"), binary = path.join(directory, api);
        fs.writeFileSync(input, `
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime/d9wg_transport.h"
#include "runtime/d9wg_process_aliases.h"
#include "openglproxy/v86gl_ioctl.h"
#define ARENA (D9WG_RESPONSE_REGION_BYTES+512u+40u)
static uint8_t memory[2][ARENA];
static D9WGProcess states[2];
typedef struct {uint8_t *bytes;uint32_t length;} Packet;
static Packet packets[2][128];static unsigned counts[2],opens,closes,unmaps;
static DWORD last_error,tick=100;static int fail_open,fail_map,fail_submit,accepted_failure;
static DWORD direct_map_error=ERROR_INVALID_FUNCTION;static unsigned direct_maps,legacy_maps;
#define TRACE(...) ((void)0)
#define D8WG_TRACE(...) ((void)0)
#define DDWG_TRACE_CHECKPOINT(...) ((void)0)
#define D9_VGL2_RECORD_HEADER_BYTES 8u
#define D8WG_VGL2_RECORD_HEADER_BYTES 8u
#define DDWG_VGL2_RECORD_HEADER_BYTES 8u
DWORD GetLastError(void){return last_error;}void SetLastError(DWORD value){last_error=value;}
DWORD GetTickCount(void){return tick;}
HANDLE CreateFileA(const char *name,DWORD access,DWORD share,void *security,DWORD mode,DWORD attrs,HANDLE template){
 (void)name;(void)access;(void)share;(void)security;(void)mode;(void)attrs;(void)template;opens++;
 if(fail_open){fail_open=0;last_error=2;return INVALID_HANDLE_VALUE;}return (HANDLE)3;
}
BOOL CloseHandle(HANDLE handle){assert(handle==(HANDLE)2||handle==(HANDLE)3);closes++;return TRUE;}
BOOL DeviceIoControl(HANDLE handle,DWORD op,const void *input,DWORD input_size,void *output,DWORD output_size,DWORD *returned,void *overlapped){
 (void)overlapped;*returned=0;
 if(op==V86GL_IOCTL_MAP_D3D_BUFFER){direct_maps++;if(direct_map_error){last_error=direct_map_error;return FALSE;}memset(output,0,output_size);*returned=output_size;return TRUE;}
 if(op==V86GL_IOCTL_MAP_BUFFER){legacy_maps++;assert(handle==(HANDLE)3);if(fail_map){fail_map=0;last_error=8;return FALSE;}assert(output_size==sizeof(V86GLMapBuffer));memset(output,0,output_size);*returned=output_size;return TRUE;}
 if(op==V86GL_IOCTL_UNMAP_BUFFER){assert(handle==(HANDLE)2);unmaps++;return TRUE;}
 assert(op==V86GL_IOCTL_SUBMIT&&input_size==sizeof(V86GLSubmit));unsigned index=(unsigned)(uintptr_t)handle-1u;assert(index<2);
 if(fail_submit&&!accepted_failure){fail_submit=0;last_error=121;return FALSE;}
 const V86GLSubmit *submit=input;assert(submit->descriptor_bytes<=ARENA&&!submit->flags&&counts[index]<128);
 Packet *packet=&packets[index][counts[index]++];packet->length=submit->descriptor_bytes;packet->bytes=malloc(packet->length);assert(packet->bytes);memcpy(packet->bytes,memory[index],packet->length);
 if(accepted_failure){accepted_failure=0;fail_submit=0;last_error=121;return FALSE;}return TRUE;
}
static BOOL open_transport_locked(void){return TRUE;}
${api === "d3d8" ? "static void d8wg_log(const char *text){(void)text;}" : ""}
${api === "ddraw" ? "static int wsprintfA(char *text,const char *format,...){(void)format;*text=0;return 0;}\nstatic void transport_failed_locked(const char *reason){(void)reason;}" : ""}
${legacy}
static BOOL flush_new(BOOL present){return d9wg_transport_submit(&states[1],present);}
static BOOL broken_flush(BOOL present){(void)present;return TRUE;}
static void record(BOOL candidate,unsigned index){
 uint32_t bytes=13u+(index*7u)%91u;uint8_t *payload,*blob;D9WGCommandHeader *header=NULL;
 BOOL ok=candidate?d9wg_transport_reserve(&states[1],D9WG_OP_UPDATE_BUFFER,sizeof(D9WGUpdateBuffer),bytes,&header,&payload,&blob,flush_new):
 reserve_command_locked(D9WG_OP_UPDATE_BUFFER,sizeof(D9WGUpdateBuffer),bytes,${api === "ddraw" ? "" : "&header,"}&payload,&blob);
 assert(ok);D9WGUpdateBuffer *update=(D9WGUpdateBuffer*)payload;
 update->resource_handle=77u+index%4u;update->destination_offset=index*128u;update->byte_count=bytes;
 update->data_offset=(uint32_t)(blob-(candidate?d9wg_transport_batch(&states[1]):batch_base()));update->lock_flags=index%5u?0:0x2000u;
 for(uint32_t i=0;i<bytes;i++)blob[i]=(uint8_t)(index*31u+i*73u);
}
int main(void){
 g_process=&states[0];memset(memory,0xcc,sizeof(memory));
 for(unsigned i=0;i<2;i++){states[i].transport=(HANDLE)(uintptr_t)(i+1u);states[i].dma_buffer=memory[i];states[i].dma_capacity=ARENA;states[i].frame_id=states[i].sequence=1;states[i].session_id_low=0x12345678;states[i].session_id_high=0x9abcdef0;}
 reset_batch_locked();d9wg_transport_reset_batch(&states[1]);
 for(unsigned candidate=0;candidate<2;candidate++){
  for(unsigned i=0;i<100;i++){record(candidate,i);if(i%11u==10u)assert(candidate?flush_new(i%22u==10u):submit_batch_locked(i%22u==10u));}
  assert(candidate?flush_new(TRUE):submit_batch_locked(TRUE));
 }
 assert(counts[0]==counts[1]&&counts[0]>20);
 for(unsigned i=0;i<counts[0];i++){assert(packets[0][i].length==packets[1][i].length);assert(!memcmp(packets[0][i].bytes,packets[1][i].bytes,packets[0][i].length));}
 for(unsigned i=0;i<2;i++)for(unsigned at=ARENA-D9WG_RESPONSE_REGION_BYTES;at<ARENA;at++)assert(memory[i][at]==0xcc);
 assert(d9wg_transport_open(&states[1])&&!opens); /* existing owner never reopens */
 uint32_t before=states[1].batch_bytes;assert(!d9wg_transport_reserve(&states[1],1,UINT32_MAX,1,0,0,0,flush_new));assert(states[1].batch_bytes==before);
 assert(!d9wg_transport_reserve(&states[1],1,0,UINT32_MAX,0,0,0,flush_new));
 states[1].batch_bytes=512;assert(!d9wg_transport_reserve(&states[1],1,8,0,0,0,0,broken_flush));d9wg_transport_reset_batch(&states[1]);
 // A refused or ambiguously accepted submission is dropped exactly once,
 // retaining mapping/generation and allowing the next batch to proceed.
 record(TRUE,100);unsigned packets_before=counts[1];uint8_t *dma=states[1].dma_buffer;
 fail_submit=1;assert(!flush_new(TRUE)&&GetLastError()==121&&counts[1]==packets_before);
 assert(states[1].dma_buffer==dma&&!states[1].command_count&&states[1].batches_dropped==1);
 record(TRUE,101);assert(flush_new(TRUE)&&counts[1]==packets_before+1);
 record(TRUE,102);accepted_failure=fail_submit=1;assert(!flush_new(FALSE)&&counts[1]==packets_before+2);
 record(TRUE,103);assert(flush_new(FALSE)&&counts[1]==packets_before+3&&states[1].batches_dropped==2);
 for(unsigned i=packets_before;i<counts[1];i++){D9WGBatchHeader *batch=(D9WGBatchHeader*)(packets[1][i].bytes+40);assert(batch->command_count==1);}
 // Host-native tests cover map failure/retry; actual 32-bit mapping success
 // is exercised separately by the XP loader/driver test.
 D9WGProcess fresh={0};fresh.transport=INVALID_HANDLE_VALUE;fail_open=1;uint32_t stage;
 assert(!d9wg_transport_open_report(&fresh,&stage)&&GetLastError()==2&&opens==1&&stage==D9WG_OPEN_FAILED);
 for(unsigned i=0;i<20;i++)assert(!d9wg_transport_open_report(&fresh,&stage)&&stage==D9WG_OPEN_DEFERRED);assert(opens==1);
 tick+=1000;fail_map=1;assert(!d9wg_transport_open_report(&fresh,&stage)&&GetLastError()==8&&opens==2&&stage==D9WG_MAP_FAILED);
 tick+=1000;assert(!d9wg_transport_open_report(&fresh,&stage)&&GetLastError()==ERROR_INVALID_DATA&&opens==3&&stage==D9WG_MAP_FAILED);
 d9wg_transport_close(&states[1]);assert(unmaps==1&&closes==3&&!states[1].dma_buffer&&states[1].transport==INVALID_HANDLE_VALUE);
 // Only unsupported new IOCTLs may fall back; BUSY/OOM must remain visible.
 for(unsigned i=0;i<4;i++){
  DWORD errors[]={ERROR_NOT_SUPPORTED,170u,8u,0u};direct_map_error=errors[i];
  D9WGProcess next={0};next.transport=INVALID_HANDLE_VALUE;tick+=1000;
  unsigned old_legacy=legacy_maps,old_direct=direct_maps;
  assert(!d9wg_transport_open_report(&next,&stage)&&stage==D9WG_MAP_FAILED);
  assert(direct_maps==old_direct+1&&legacy_maps==old_legacy+(i==0));
  assert(GetLastError()==(i==0||i==3?ERROR_INVALID_DATA:errors[i]));
 }
 printf("PASS ${api} frozen C framing: %u byte-identical batches; overflow, retry, mapping retention and ambiguous acceptance pass\\n",counts[0]);
 for(unsigned k=0;k<2;k++)for(unsigned i=0;i<counts[k];i++)free(packets[k][i].bytes);return 0;
}
`);
        const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-I", path.join(__dirname, "native"), "-I", root,
            input, path.join(root, "runtime/d9wg_transport.c"), path.join(root, "runtime/d9wg_runtime.c"), "-o", binary], { encoding: "utf8" });
        assert.equal(built.status, 0, built.stderr || String(built.error));
        const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr || String(run.error));process.stdout.write(run.stdout);
    }
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
