"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-"));
try{fs.writeFileSync(dir+'/parameters.c',`#include <stdint.h>
#include <stdio.h>
#include "d9wg_present_pacing.h"
#define CHECK(v) do{if(!(v)){fprintf(stderr,"Present parameters line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){uint32_t requests[]={0,1,2,4,8,0x80000000u,3,5,6,9,16,0x40000000u,0xffffffffu,0x80000001u},bits[]={1,2,4,8,0x80000000u};unsigned cases=0;CHECK(D9WG_PRESENT_INTERVAL_MASK==0x80000001u);cases++;
for(uint32_t api=8;api<=9;api++)for(int windowed=0;windowed<2;windowed++)for(unsigned subset=0;subset<32;subset++){uint32_t mask=0;for(unsigned b=0;b<5;b++)if(subset&(1u<<b))mask|=bits[b];for(unsigned i=0;i<14;i++){uint32_t request=requests[i];int expected=request==0;for(unsigned b=0;b<5;b++)if(request==bits[b])expected=(subset&(1u<<b))!=0;if(windowed){if(api==8)expected=!request;else if(request==2||request==4||request==8)expected=0;}CHECK(d9wg_present_interval_valid(api,windowed,request,mask)==expected);cases++;}}
uint32_t bad_api[]={0,7,10,0xffffffffu};for(unsigned i=0;i<4;i++){CHECK(!d9wg_present_interval_valid(bad_api[i],0,0,0xffffffffu));cases++;}
printf("PASS shared presentation parameters: %u D8/D9, window/fullscreen, capability-subset, default, single-flag and invalid-bit cases\\n",cases);return 0;}
`);const b=spawnSync(process.env.HOST_CC||'cc',['-std=c11','-Os','-Wall','-Wextra','-Werror','-I',root+'/runtime',dir+'/parameters.c',process.env.D9WG_PARAMETERS_RUNTIME||root+'/runtime/d9wg_present_pacing.c','-o',dir+'/parameters'],{encoding:'utf8'});assert.equal(b.status,0,b.stderr);const r=spawnSync(dir+'/parameters',[],{encoding:'utf8'});assert.equal(r.status,0,r.stderr+' signal='+r.signal);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
