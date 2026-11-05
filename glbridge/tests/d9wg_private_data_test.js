"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-"));
try{fs.writeFileSync(dir+'/private.c',`#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d9wg_private_data.h"
typedef struct Object Object;
struct Object{atomic_uint refs;int dead;void(*on_add)(Object*),(*on_zero)(Object*);};
typedef struct{pthread_mutex_t mutex;atomic_uint allocations;int fail;}Context;
static Context context={.mutex=PTHREAD_MUTEX_INITIALIZER};
static _Thread_local int held;
typedef union{max_align_t alignment;struct{uint32_t size;}data;}Allocation;
static void*allocate(void*value,uint32_t size){Context*c=value;assert(!held);if(c->fail||size>1048576)return NULL;Allocation*p=malloc(sizeof(*p)+size);assert(p);p->data.size=size;atomic_fetch_add(&c->allocations,1);return p+1;}
static void release_memory(void*value,void*data){Context*c=value;assert(!held&&data);Allocation*p=(Allocation*)data-1;memset(data,0xdd,p->data.size);assert(atomic_fetch_sub(&c->allocations,1));free(p);}
static void lock(void*value){Context*c=value;assert(!held);assert(!pthread_mutex_lock(&c->mutex));held=1;}
static void unlock(void*value){Context*c=value;assert(held);held=0;assert(!pthread_mutex_unlock(&c->mutex));}
static void retain(void*value,void*pointer){(void)value;Object*o=pointer;assert(!held&&!o->dead&&atomic_fetch_add(&o->refs,1)>0);if(o->on_add){void(*hook)(Object*)=o->on_add;o->on_add=NULL;hook(o);}}
static void release(void*value,void*pointer){(void)value;Object*o=pointer;assert(!held);unsigned before=atomic_fetch_sub(&o->refs,1);assert(before&&!o->dead);if(before==1){o->dead=1;if(o->on_zero)o->on_zero(o);}}
static const D9WGPrivateOps ops={&context,allocate,release_memory,retain,release,lock,unlock};
static const uint8_t key[16]={1},second[16]={2},missing[16]={3};
static D9WGPrivateStore store;
static Object*replacement;
static unsigned hooks;
static void remove_during_get(Object*object){(void)object;assert(!d9wg_private_free(&store,key,&ops));hooks++;}
static void inspect_replacement(Object*object){(void)object;Object*out=NULL;uint32_t size=sizeof(out);assert(!d9wg_private_get(&store,key,&out,&size,&ops)&&out==replacement);release(NULL,out);hooks++;}
static void insert_during_clear(Object*object){(void)object;uint32_t value=0x1234abcd;assert(d9wg_private_get(&store,key,NULL,NULL,&ops)==D9WG_PRIVATE_NOT_FOUND);assert(!d9wg_private_set(&store,second,&value,4,0,&ops));hooks++;}
static Object shared={.refs=ATOMIC_VAR_INIT(1)};
static void*worker(void*value){uintptr_t id=(uintptr_t)value;for(uint32_t i=0;i<2000;i++){uint32_t data[2]={(uint32_t)(id*100000+i),~(uint32_t)(id*100000+i)},out[2];assert(!d9wg_private_set(&store,key,data,8,0,&ops));uint32_t size=8;int r=d9wg_private_get(&store,key,out,&size,&ops);assert(r==D9WG_PRIVATE_NOT_FOUND||(!r&&size==8&&out[1]==~out[0]));assert(!d9wg_private_set(&store,second,&shared,sizeof(void*),1,&ops));Object*object=NULL;size=sizeof(object);r=d9wg_private_get(&store,second,&object,&size,&ops);assert(r==D9WG_PRIVATE_NOT_FOUND||(!r&&object==&shared&&size==sizeof(object)));if(!r)release(NULL,object);if(!(i%31))d9wg_private_clear(&store,&ops);}return NULL;}
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Private data line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){unsigned cases=0;uint8_t data[32],out[32];uint32_t size=0xdeadbeef;memset(out,0xa5,sizeof(out));CHECK(d9wg_private_get(&store,missing,out,&size,&ops)==D9WG_PRIVATE_NOT_FOUND&&size==0xdeadbeef&&out[0]==0xa5);CHECK(d9wg_private_get(&store,missing,NULL,NULL,&ops)==D9WG_PRIVATE_NOT_FOUND);CHECK(d9wg_private_free(&store,missing,&ops)==D9WG_PRIVATE_NOT_FOUND);cases++;
 for(unsigned i=0;i<16;i++)data[i]=(uint8_t)(i*13+7);CHECK(!d9wg_private_set(&store,key,data,16,0x80000000u,&ops));memset(data,0,sizeof(data));size=1;CHECK(!d9wg_private_get(&store,key,NULL,&size,&ops)&&size==16);size=15;CHECK(d9wg_private_get(&store,key,out,&size,&ops)==D9WG_PRIVATE_MORE_DATA&&size==16&&out[0]==0xa5);size=32;CHECK(!d9wg_private_get(&store,key,out,&size,&ops)&&size==16&&out[16]==0xa5);for(unsigned i=0;i<16;i++)CHECK(out[i]==(uint8_t)(i*13+7));cases++;
 CHECK(!d9wg_private_set(&store,second,NULL,0,0,&ops));size=0;CHECK(!d9wg_private_get(&store,second,NULL,&size,&ops)&&!size);CHECK(!d9wg_private_get(&store,second,out,&size,&ops));CHECK(!d9wg_private_free(&store,second,&ops));cases++;
 uint32_t word=0xcafebabe;CHECK(!d9wg_private_set(&store,key,&word,4,0,&ops));uint32_t overlapping=4;CHECK(!d9wg_private_get(&store,key,&overlapping,&overlapping,&ops)&&overlapping==word);uint8_t unaligned[6]={0xaa,4,0,0,0,0xbb};CHECK(!d9wg_private_get(&store,key,out,unaligned+1,&ops)&&!memcmp(out,&word,4)&&unaligned[0]==0xaa&&unaligned[5]==0xbb);cases++;
 Object a={.refs=ATOMIC_VAR_INIT(1)},b={.refs=ATOMIC_VAR_INIT(1)};for(unsigned i=0;i<3;i++){uint32_t bad[]={0,5,sizeof(void*)*2};CHECK(d9wg_private_set(&store,key,&a,bad[i],1,&ops)==D9WG_PRIVATE_INVALID);size=4;CHECK(!d9wg_private_get(&store,key,out,&size,&ops)&&!memcmp(out,&word,4));cases++;}
 CHECK(!d9wg_private_set(&store,key,&a,sizeof(void*),1,&ops)&&atomic_load(&a.refs)==2);Object*pointer=(void*)1;size=1;CHECK(!d9wg_private_get(&store,key,NULL,&size,&ops)&&size==sizeof(pointer)&&atomic_load(&a.refs)==2);size=sizeof(pointer)-1;CHECK(d9wg_private_get(&store,key,&pointer,&size,&ops)==D9WG_PRIVATE_MORE_DATA&&pointer==(void*)1&&size==sizeof(pointer)&&atomic_load(&a.refs)==2);size=sizeof(pointer)*2;CHECK(!d9wg_private_get(&store,key,&pointer,&size,&ops)&&pointer==&a&&size==sizeof(pointer)&&atomic_load(&a.refs)==3);release(NULL,pointer);cases++;
 context.fail=1;CHECK(d9wg_private_set(&store,key,&word,4,0,&ops)==D9WG_PRIVATE_OUT_OF_MEMORY&&atomic_load(&a.refs)==2);context.fail=0;CHECK(d9wg_private_set(&store,key,&word,UINT32_MAX,0,&ops)==D9WG_PRIVATE_OUT_OF_MEMORY);cases++;
 release(NULL,&a);CHECK(atomic_load(&a.refs)==1);CHECK(!d9wg_private_set(&store,key,&a,sizeof(void*),1,&ops)&&!a.dead&&atomic_load(&a.refs)==1);replacement=&b;a.on_zero=inspect_replacement;CHECK(!d9wg_private_set(&store,key,&b,sizeof(void*),1,&ops)&&a.dead&&hooks==1&&atomic_load(&b.refs)==2);cases++;
 b.on_add=remove_during_get;size=sizeof(pointer);CHECK(!d9wg_private_get(&store,key,&pointer,&size,&ops)&&pointer==&b&&hooks==2&&atomic_load(&b.refs)==2&&!store.head);release(NULL,pointer);CHECK(atomic_load(&b.refs)==1);cases++;
 CHECK(!d9wg_private_set(&store,key,&b,sizeof(void*),1,&ops));release(NULL,&b);b.on_zero=insert_during_clear;d9wg_private_clear(&store,&ops);CHECK(b.dead&&hooks==3);size=4;CHECK(!d9wg_private_get(&store,second,&word,&size,&ops)&&word==0x1234abcd);d9wg_private_clear(&store,&ops);CHECK(!atomic_load(&context.allocations));cases++;
 uint8_t zero[16]={0};CHECK(!d9wg_private_set(&store,zero,&word,4,2,&ops));CHECK(d9wg_private_get(&store,zero,NULL,NULL,&ops)==D9WG_PRIVATE_INVALID);CHECK(d9wg_private_set(&store,key,NULL,1,0,&ops)==D9WG_PRIVATE_INVALID);CHECK(d9wg_private_set(&store,NULL,&word,4,0,&ops)==D9WG_PRIVATE_INVALID);d9wg_private_clear(&store,&ops);cases++;
 /* Distinct GUIDs must coexist deterministically; thread scheduling is not
  * evidence that comparisons examine all sixteen bytes. */
 D9WGPrivateStore tags={0};for(unsigned i=0;i<16;i++){uint8_t tag[16]={0};tag[i]=(uint8_t)(i+1);uint32_t value=0xa5c00000u+i;CHECK(!d9wg_private_set(&tags,tag,&value,sizeof(value),0,&ops));}
 for(unsigned i=0;i<16;i++){uint8_t tag[16]={0};tag[i]=(uint8_t)(i+1);uint32_t value=0,size=sizeof(value);CHECK(!d9wg_private_get(&tags,tag,&value,&size,&ops)&&size==sizeof(value)&&value==0xa5c00000u+i);cases++;}
 uint8_t absent[16]={0};uint32_t untouched=0xdeadbeef,capacity=sizeof(untouched);CHECK(d9wg_private_get(&tags,absent,&untouched,&capacity,&ops)==D9WG_PRIVATE_NOT_FOUND&&untouched==0xdeadbeef&&capacity==sizeof(untouched));d9wg_private_clear(&tags,&ops);CHECK(!tags.head&&!atomic_load(&context.allocations));
 pthread_t threads[4];for(uintptr_t i=0;i<4;i++)CHECK(!pthread_create(threads+i,NULL,worker,(void*)(i+1)));for(unsigned i=0;i<4;i++)CHECK(!pthread_join(threads[i],NULL));d9wg_private_clear(&store,&ops);CHECK(!atomic_load(&context.allocations)&&atomic_load(&shared.refs)==1&&!shared.dead&&!store.head);release(NULL,&shared);CHECK(shared.dead);cases++;
 printf("PASS shared private data: %u byte/interface/error/alias/reentrant cases, 8000 concurrent Set/Get/Clear iterations, immutable pins, exact refs and zero allocations\\n",cases);return 0;}
`);const build=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-pthread','-I',root+'/runtime',dir+'/private.c',process.env.D9WG_PRIVATE_STORE_SOURCE||root+'/runtime/d9wg_private_data.c','-o',dir+'/private'],{encoding:'utf8'});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+'/private',[],{encoding:'utf8',timeout:30000});assert(!run.error,run.error);assert.equal(run.status,0,run.stderr+' signal='+run.signal);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
