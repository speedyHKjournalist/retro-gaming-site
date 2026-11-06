'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.D9WG_QUERY_SOURCE||root+'/d3d9proxy/d3d9_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-query-'));
function extract(name){const m=new RegExp('^static[^;{}]*\\b'+name+'\\([^;]*?\\)\\s*\\{','m').exec(source);assert(m,name);let e=source.indexOf('{',m.index)+1,d=1;for(;d;e++){if(source[e]==='{')d++;if(source[e]==='}')d--;}return source.slice(m.index,e);}
try {
fs.writeFileSync(dir+'/test.c',`#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "${root}/d3d9proxy/d3d9_protocol.h"
typedef uint32_t DWORD;typedef uint64_t ULONGLONG;typedef int BOOL,HRESULT;
#define WINAPI
#define TRUE 1
#define FALSE 0
#define S_OK 0
#define D3D_OK 0
#define S_FALSE 1
#define D3DERR_INVALIDCALL (-1)
#define D3DERR_DRIVERINTERNALERROR (-2)
#define D3DQUERYTYPE_EVENT 8
#define D3DQUERYTYPE_OCCLUSION 9
#define D3DQUERYTYPE_TIMESTAMP 10
#define D3DQUERYTYPE_TIMESTAMPDISJOINT 11
#define D3DQUERYTYPE_TIMESTAMPFREQ 12
#define D3DISSUE_END 1
#define D3DISSUE_BEGIN 2
#define D3DGETDATA_FLUSH 1
#define TRACE_REFUSE(v) (v)
#define TRACE(...)
#define HOSTLOG_FAILED(...)
#define CopyMemory memcpy
#define MemoryBarrier() ((void)0)
struct Device{struct{DWORD Width,Height;}display_mode;};
typedef struct{struct Device*device;DWORD type,handle,request_id,last_beat,silent_since;BOOL begun,issued;}D9Query,IDirect3DQuery9;
static DWORD next_id,ticks=10000,beat,emits,last_op,flushes,checks;static BOOL failed_emit,missing_response;
static int g_transport_lock;static DWORD g_command_count;static D9WGQueryResponse response;
static D9Query*query_from_iface(D9Query*q){return q;}
static DWORD next_response_request_id(void){return ++next_id;}
static DWORD query_heartbeat(void){return beat;}
static DWORD GetTickCount(void){return ticks;}
static D9WGQueryResponse*query_response(D9Query*q){(void)q;return missing_response?NULL:&response;}
static BOOL emit_query_issue(D9Query*q,uint16_t op){(void)q;++emits;last_op=op;return !failed_emit;}
static void EnterCriticalSection(int*p){(void)p;}static void LeaveCriticalSection(int*p){(void)p;}
static void submit_batch_locked(BOOL present){assert(!present);++flushes;g_command_count=0;}
${['query_get_data_size','query_issue','query_fallback_value','query_get_data'].map(extract).join('\n')}
#define CHECK(v) do{++checks;if(!(v)){fprintf(stderr,"Query contract line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){struct Device d={{640,480}};
for(DWORD type=8;type<=12;type++){
 D9Query q={.device=&d,.type=type,.handle=7};BOOL interval=type==9||type==11;emits=flushes=0;
 CHECK(query_get_data_size(&q)==(type==10||type==12?8u:4u));
 for(DWORD size=0;size<=16;size++)for(DWORD offset=0;offset<8;offset++){
  unsigned char data[32];memset(data,0x5a,sizeof(data));CHECK(query_get_data(&q,data+offset,size,1)==S_OK);
  for(DWORD i=0;i<32;i++)CHECK(data[i]==(i>=offset&&i<offset+(size<8?size:8)?0xdd:0x5a));
 }
 CHECK(!emits&&!flushes);CHECK(query_get_data(&q,NULL,0,0)==S_OK);
 CHECK(query_get_data(&q,NULL,1,1)==D3DERR_INVALIDCALL);
 CHECK(query_get_data(&q,NULL,0,2)==D3DERR_INVALIDCALL);
 CHECK(query_issue(&q,0)==D3DERR_INVALIDCALL&&query_issue(&q,3)==D3DERR_INVALIDCALL);
 CHECK(!query_issue(&q,D3DISSUE_BEGIN));CHECK(emits==(DWORD)interval&&q.begun==interval);
 CHECK(!query_issue(&q,D3DISSUE_BEGIN));CHECK(emits==2u*interval&&q.begun==interval);
 if(interval){unsigned char out[8];memset(out,0x5a,8);g_command_count=1;CHECK(query_get_data(&q,out,8,1)==S_FALSE&&flushes==1);for(int i=0;i<8;i++)CHECK(out[i]==0x5a);}
 CHECK(!query_issue(&q,D3DISSUE_END)&&!q.begun&&q.issued&&last_op==D9WG_OP_END_QUERY);
 CHECK(query_get_data(&q,NULL,0,1)==S_FALSE);
 CHECK(!query_issue(&q,D3DISSUE_END)&&!q.begun&&q.issued);
 response.request_id=q.request_id;response.status=D9WG_RESPONSE_OK;response.value_low=0x78563412;response.value_high=0x12345678;
 unsigned char expected[8]={0x12,0x34,0x56,0x78,0x78,0x56,0x34,0x12};
 for(DWORD size=0;size<=16;size++)for(DWORD offset=0;offset<8;offset++){
  unsigned char data[32];memset(data,0x5a,sizeof(data));CHECK(query_get_data(&q,data+offset,size,0)==S_OK);
  for(DWORD i=0;i<32;i++)CHECK(data[i]==(i>=offset&&i<offset+(size<8?size:8)?expected[i-offset]:0x5a));
 }
 /* A late completion for an earlier generation is never accepted. */
 response.request_id=q.request_id-1;CHECK(query_get_data(&q,NULL,0,1)==S_FALSE);response.request_id=q.request_id;
 /* Failed and silent replies both use the selected fallback value, including
  * partial/unaligned copies; they must not leak a stale response low word. */
 response.status=2;ULONGLONG out=0;CHECK(!query_get_data(&q,&out,8,1)&&out==query_fallback_value(&q));
 response.status=D9WG_RESPONSE_PENDING;q.silent_since=ticks-6000;CHECK(!query_get_data(&q,&out,8,1)&&out==query_fallback_value(&q));
 ++beat;CHECK(query_get_data(&q,NULL,0,1)==S_FALSE&&q.silent_since==ticks);
 missing_response=1;CHECK(query_get_data(&q,NULL,0,1)==D3DERR_DRIVERINTERNALERROR);missing_response=0;
}
printf("PASS native query contracts: %u checks; five query kinds, initial/open/pending/completed/stale/failed/silent states, repeated BEGIN/END, partial and unaligned writes with guards\\n",checks);return 0;}
`);
let r=spawnSync('cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);
r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);process.stdout.write(r.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
