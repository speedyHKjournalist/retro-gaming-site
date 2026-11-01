"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), os = require("node:os"), path = require("node:path");
const { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), directory = fs.mkdtempSync(path.join(os.tmpdir(), "d9wg-owner-"));
function definition(source, signature) {
    const start = source.indexOf(signature);assert(start >= 0, signature);
    let at = source.indexOf("{", start), depth = 1;
    for (at++; depth; at++) { if (source[at] === "{") depth++;else if (source[at] === "}") depth--; }
    return source.slice(start, at);
}
try {
    fs.copyFileSync(path.join(__dirname, "native/windows.h"), path.join(directory, "windows.h"));
    const modules = [];
    for (const api of ["d3d9", "d3d8", "ddraw"]) {
        const source = fs.readFileSync(path.join(root, api + "proxy/" + api + "_proxy.c"), "utf8");
        const input = path.join(directory, api + ".c");modules.push(input);
        fs.writeFileSync(input, `
#include <assert.h>
#include <string.h>
#include "runtime/d9wg_process_aliases.h"
#define CopyMemory memcpy
#define ZeroMemory(p,n) memset(p,0,n)
#define TRACE(...) ((void)0)
#define TRACE_FLUSH() ((void)0)
#define DDWG_TRACE_CHECKPOINT(...) ((void)0)
extern unsigned submits,transport_closes,session_ends;
${api === "d3d9" ? "static HINSTANCE g_module_instance;" : ""}
${api === "ddraw" ? `static HINSTANCE g_instance;static CRITICAL_SECTION g_object_lock;
typedef struct DDrawObject {struct DDrawObject *next;} DDrawObject;static DDrawObject *g_objects;
static void emit_window_state_not_showing(DDrawObject *object){(void)object;}
static BOOL emit_command(uint16_t opcode,const void *payload,uint32_t bytes){
 const D9WGSessionEnd *end=payload;assert(opcode==D9WG_OP_SESSION_END&&bytes==sizeof(*end));
 assert(g_process->references==1&&end->session_id_low==g_session_id_low&&end->session_id_high==g_session_id_high);
 session_ends++;g_command_count++;return TRUE;
}
${definition(source, "static void emit_session_end(")}` : `static BOOL reserve_command_locked(uint16_t opcode,uint32_t size,uint32_t extra,
 D9WGCommandHeader **command,uint8_t **payload,uint8_t **blob){
 static uint8_t out[64];(void)command;(void)blob;assert(g_transport_lock.held&&g_process->references==1);
 assert(opcode==D9WG_OP_SESSION_END&&size==sizeof(D9WGSessionEnd)&&!extra);*payload=out;session_ends++;g_command_count++;return TRUE;
}`}
static BOOL submit_batch_locked(BOOL present){(void)present;assert(g_transport_lock.held);submits++;g_command_count=0;g_batch_bytes=sizeof(D9WGBatchHeader);return TRUE;}
static void close_transport_locked(void){assert(g_transport_lock.held&&g_process->references==1);transport_closes++;g_transport=INVALID_HANDLE_VALUE;g_dma_buffer=NULL;}
${definition(source, "static uint32_t allocate_handle(")}
#define DllMain ${api}_entry
${definition(source, "BOOL WINAPI DllMain(")}
D9WGProcess *${api}_state(void){return g_process;}
uint32_t ${api}_allocate(void){return allocate_handle();}
void ${api}_append(uint32_t bytes){EnterCriticalSection(&g_transport_lock);g_batch_bytes+=bytes;g_command_count++;LeaveCriticalSection(&g_transport_lock);}
`);
    }
    const input = path.join(directory, "main.c"), binary = path.join(directory, "test");
    fs.writeFileSync(input, `
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include "runtime/d9wg_process.h"
typedef struct Mapping {char name[64];int fd;unsigned handles,views;size_t bytes;} Mapping;
static Mapping *maps[16];static struct {const void *address;Mapping *owner;} views[64];
static unsigned mappings_created,mappings_destroyed,sections_created,sections_deleted;
static int fail_open,fail_map;static DWORD pid=1234,ticks=0x12345678;
unsigned submits,transport_closes,session_ends;
DWORD GetCurrentProcessId(void){return pid;}DWORD GetTickCount(void){return ticks;}
void GetSystemTimeAsFileTime(FILETIME *time){time->dwLowDateTime=0x1234;time->dwHighDateTime=0x5678;}
BOOL QueryPerformanceCounter(LARGE_INTEGER *counter){static int64_t next=123;counter->QuadPart=next++;return TRUE;}
LONG InterlockedIncrement(volatile LONG *value){return ++*value;}LONG InterlockedDecrement(volatile LONG *value){return --*value;}
void InitializeCriticalSection(CRITICAL_SECTION *section){section->held=0;sections_created++;}
void DeleteCriticalSection(CRITICAL_SECTION *section){assert(!section->held);sections_deleted++;}
void EnterCriticalSection(CRITICAL_SECTION *section){section->held++;}void LeaveCriticalSection(CRITICAL_SECTION *section){assert(section->held);section->held--;}
BOOL DisableThreadLibraryCalls(HINSTANCE instance){(void)instance;return TRUE;}
static void retire(Mapping *mapping){if(mapping->handles||mapping->views)return;for(unsigned i=0;i<16;i++)if(maps[i]==mapping)maps[i]=0;close(mapping->fd);free(mapping);mappings_destroyed++;}
HANDLE CreateFileMappingA(HANDLE file,void *security,DWORD protection,DWORD high,DWORD bytes,const char *name){
 (void)security;assert(file==INVALID_HANDLE_VALUE&&protection==PAGE_READWRITE&&!high);
 if(fail_open){fail_open=0;return NULL;}
 for(unsigned i=0;i<16;i++)if(maps[i]&&!strcmp(maps[i]->name,name)){maps[i]->handles++;return maps[i];}
 Mapping *mapping=calloc(1,sizeof(*mapping));assert(mapping);char temporary[]=${JSON.stringify(path.join(directory, "mapping.XXXXXX"))};
 mapping->fd=mkstemp(temporary);assert(mapping->fd>=0);unlink(temporary);assert(!ftruncate(mapping->fd,bytes));
 strcpy(mapping->name,name);mapping->bytes=bytes;mapping->handles=1;
 for(unsigned i=0;i<16;i++)if(!maps[i]){maps[i]=mapping;mappings_created++;return mapping;}abort();
}
void *MapViewOfFile(HANDLE handle,DWORD access,DWORD high,DWORD low,size_t bytes){
 Mapping *mapping=handle;assert(access==FILE_MAP_ALL_ACCESS&&!high&&!low&&bytes==mapping->bytes);
 if(fail_map){fail_map=0;return NULL;}
 void *address=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,mapping->fd,0);assert(address!=MAP_FAILED);
 for(unsigned i=0;i<64;i++)if(!views[i].address){views[i].address=address;views[i].owner=mapping;mapping->views++;return address;}abort();
}
BOOL UnmapViewOfFile(const void *address){for(unsigned i=0;i<64;i++)if(views[i].address==address){Mapping *mapping=views[i].owner;views[i].address=0;assert(!munmap((void*)address,mapping->bytes));mapping->views--;retire(mapping);return TRUE;}abort();}
BOOL CloseHandle(HANDLE handle){Mapping *mapping=handle;assert(mapping->handles);mapping->handles--;retire(mapping);return TRUE;}
#define FRONTEND(name) BOOL name##_entry(HINSTANCE,DWORD,LPVOID);D9WGProcess *name##_state(void);uint32_t name##_allocate(void);void name##_append(uint32_t);
FRONTEND(d3d9) FRONTEND(d3d8) FRONTEND(ddraw)
int main(void){
 assert(d3d9_entry((void*)1,DLL_PROCESS_ATTACH,NULL));D9WGProcess *owner=d3d9_state();
 assert(owner->references==1&&owner->transport==INVALID_HANDLE_VALUE);owner->transport=(HANDLE)7;owner->dma_buffer=(uint8_t*)8;owner->batch_bytes=32;
 uint32_t first=d3d9_allocate();d3d9_append(16);uint32_t low=owner->session_id_low,high=owner->session_id_high;
 assert(d3d8_entry((void*)2,DLL_PROCESS_ATTACH,NULL));assert(d3d8_state()==owner&&owner->batch_bytes==48);
 uint32_t second=d3d8_allocate();d3d8_append(24);
 assert(ddraw_entry((void*)3,DLL_PROCESS_ATTACH,NULL));assert(ddraw_state()==owner&&owner->references==3);
 uint32_t third=ddraw_allocate();ddraw_append(32);
 assert(first+1==second&&second+1==third&&owner->batch_bytes==104&&owner->command_count==3);
 assert(owner->session_id_low==low&&owner->session_id_high==high&&mappings_created==1);
 assert(d3d9_entry(0,DLL_PROCESS_DETACH,NULL));assert(owner->references==2&&!transport_closes&&!session_ends&&owner->transport==(HANDLE)7);
 d3d8_append(16);assert(d3d8_entry(0,DLL_PROCESS_DETACH,NULL));assert(owner->references==1&&!transport_closes&&!session_ends);
 ddraw_append(16);assert(ddraw_entry(0,DLL_PROCESS_DETACH,NULL));assert(transport_closes==1&&session_ends==1&&submits==3);
 assert(mappings_destroyed==1&&sections_created==sections_deleted);
 // The creator can also be DirectDraw; unloading it first cannot retire D3D9.
 assert(ddraw_entry((void*)3,DLL_PROCESS_ATTACH,NULL));owner=ddraw_state();owner->dma_buffer=(uint8_t*)8;
 assert(d3d9_entry((void*)1,DLL_PROCESS_ATTACH,NULL));assert(d3d9_state()==owner);
 assert(ddraw_entry(0,DLL_PROCESS_DETACH,NULL));assert(owner->references==1&&transport_closes==1);
 assert(d3d9_entry(0,DLL_PROCESS_DETACH,NULL));assert(transport_closes==2&&session_ends==2&&mappings_destroyed==2);
 fail_open=1;assert(!d3d8_entry(0,DLL_PROCESS_ATTACH,NULL));assert(d3d8_entry(0,DLL_PROCESS_DETACH,NULL));
 fail_map=1;assert(!d3d8_entry(0,DLL_PROCESS_ATTACH,NULL));assert(d3d8_entry(0,DLL_PROCESS_DETACH,NULL));
 // Distinct PIDs never share canonical state, even in this one host harness.
 D9WGProcess *one=d9wg_process_attach();pid++;D9WGProcess *two=d9wg_process_attach();assert(one!=two&&one->process_id!=two->process_id);d9wg_process_release(two);d9wg_process_release(one);pid--;
 // Process termination may leave another thread holding the lock. Actual
 // DllMain bodies must defer to OS handle cleanup without trying to enter it.
 assert(d3d9_entry(0,DLL_PROCESS_ATTACH,NULL));assert(ddraw_entry(0,DLL_PROCESS_ATTACH,NULL));owner=d3d9_state();owner->transport_lock.held=1;
 unsigned before=transport_closes;assert(d3d9_entry(0,DLL_PROCESS_DETACH,(void*)1));assert(ddraw_entry(0,DLL_PROCESS_DETACH,(void*)1));assert(transport_closes==before&&owner->references==2);
 owner->transport_lock.held=0;d9wg_process_release(owner);d9wg_process_release(owner);
 assert(mappings_created==mappings_destroyed);
 puts("PASS actual C DLL entries: one process owner, canonical mapping survives creator unload, shared cursor/namespace/session, last-owner cleanup, failed attach and termination paths");return 0;
}
`);
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c99", "-D_DARWIN_C_SOURCE", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-I", directory, "-I", root,
        ...modules, input, path.join(root, "runtime/d9wg_process.c"), "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });assert.equal(run.status, 0, run.stderr || String(run.error));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
