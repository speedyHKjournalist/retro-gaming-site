"use strict";
const fs=require("node:fs"),path=require("node:path"),os=require("node:os"),assert=require("node:assert/strict"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),native=require(path.resolve(root,"../../v86/tests/glbridge/fixtures/d3d7_native_light_state.json"));
const defaults=native.cases.filter(r=>r.op===1);assert.equal(defaults.length,5);
const source=process.env.D9WG_LIGHT_STATE_HEADER||path.join(root,"runtime/d9wg_light_state.h");
const code=`#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int BOOL;
typedef struct Light {uint32_t words[26];} Light;
#define D9WG_LIGHTING_SDK_LIGHT Light
static unsigned allocations,frees,fail;
static void *allocate(size_t bytes){if(fail)return NULL;allocations++;return calloc(1,bytes);}
static void release(void *pointer){frees++;free(pointer);}
#define D9WG_LIGHT_ALLOC(bytes) allocate(bytes)
#define D9WG_LIGHT_FREE(pointer) release(pointer)
#include "${source}"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"light state line %u\\n",__LINE__);return 1;}}while(0)
static const uint32_t expected[][26]={${defaults.map(r=>'{'+r.words.map(n=>n+'u').join(',')+'}').join(',')}};
int main(void){
 struct {uint64_t before;Light light;uint64_t after;} guarded;memset(&guarded,0xcc,sizeof(guarded));d9wg_light_default(&guarded.light);
 CHECK(guarded.before==UINT64_C(0xcccccccccccccccc)&&guarded.after==UINT64_C(0xcccccccccccccccc));
 for(unsigned i=0;i<5;++i)CHECK(!memcmp(guarded.light.words,expected[i],104));
 D9WGLightState *list=NULL;CHECK(!d9wg_light_find(list,37));CHECK(!d9wg_light_get(&list,37,0));CHECK(!allocations);
 const uint32_t keys[]={0,7,8,37,1000,0xffffffffu,0xfffffffeu};
 for(unsigned i=0;i<7;++i){D9WGLightState *entry=d9wg_light_get(&list,keys[i],1);CHECK(entry&&entry->index==keys[i]&&!entry->set&&!entry->enabled&&!entry->light_mask&&!entry->enable_mask);d9wg_light_default(&entry->light);entry->light.words[1]=i;entry->set=1;}
 CHECK(allocations==7);
 for(unsigned i=0;i<7;++i){D9WGLightState *entry=d9wg_light_get(&list,keys[i],1);CHECK(entry&&entry->light.words[1]==i&&entry->set);}
 CHECK(allocations==7);
 D9WGLightState *head=list;fail=1;CHECK(!d9wg_light_get(&list,123456,1));CHECK(list==head&&!d9wg_light_find(list,123456));fail=0;
 for(unsigned i=0;i<1000;++i){uint32_t key=10000u+i*100003u;D9WGLightState *entry=d9wg_light_get(&list,key,1);CHECK(entry);entry->light.words[2]=i;entry->enabled=i%2;}
 for(unsigned i=0;i<1000;++i){D9WGLightState *entry=d9wg_light_find(list,10000u+i*100003u);CHECK(entry&&entry->light.words[2]==i&&entry->enabled==(int)(i%2));}
 d9wg_light_free(&list);CHECK(!list&&frees==allocations&&allocations==1007);d9wg_light_free(&list);CHECK(frees==allocations);return 0;
}
`;
const directory=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-lights-"));
try{
 const input=path.join(directory,"lights.c"),binary=path.join(directory,"lights");fs.writeFileSync(input,code);
 const built=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror",input,"-o",binary],{encoding:"utf8"});assert.equal(built.status,0,built.stderr||String(built.error));
 const run=spawnSync(binary,[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr||String(run.error));
 console.log("PASS shared light state: five frozen native default tables, exact bounds, 1007 sparse DWORD keys, reuse, atomic allocation failure and complete/idempotent release");
}finally{fs.rmSync(directory,{recursive:true,force:true});}
