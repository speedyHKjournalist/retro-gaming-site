"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-copy-plan-"));
try{const code=`#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "runtime/d9wg_runtime.h"
#define CHECK(v) do{if(!(v)){fprintf(stderr,"texture copy plan line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
static uint32_t seed=0x923e57a1;
static uint32_t random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static uint32_t half(uint32_t n){return n>1?n/2:1;}
static unsigned calls[3],fail_phase,fail_at,last_phase;static D9WGTextureCopyPlan expected;
static int visit(unsigned phase,uint32_t layer,uint32_t source,uint32_t destination){unsigned ordinal=calls[phase]++;if(phase<last_phase||layer!=ordinal/expected.levels||destination!=ordinal%expected.levels||source!=expected.source_first+destination)return 99;last_phase=phase;return phase==fail_phase&&calls[phase]==fail_at?(phase==0?1:phase==1?3:2):0;}
static int validate(void*c,uint32_t l,uint32_t s,uint32_t d){(void)c;return visit(0,l,s,d);}static int prepare(void*c,uint32_t l,uint32_t s,uint32_t d){(void)c;return visit(1,l,s,d);}static int copy(void*c,uint32_t l,uint32_t s,uint32_t d){(void)c;return visit(2,l,s,d);}
int main(void){
 D9WGTextureCopyPlan out,before={13,17};
 for(unsigned test=0;test<100000;++test){
  D9WGTextureCopyExtent source={1+random_word()%4096,1+random_word()%4096,1+random_word()%64,1+random_word()%14},destination=source;
  if(random_word()&1){unsigned steps=random_word()%15;for(unsigned i=0;i<steps;++i){destination.width=half(destination.width);destination.height=half(destination.height);destination.depth=half(destination.depth);}}
  else{destination.width=1+random_word()%4096;destination.height=1+random_word()%4096;destination.depth=1+random_word()%64;}
  destination.levels=1+random_word()%14;
  /* Enumerate each axis separately instead of reproducing the max-size rule. */
  D9WGTextureCopyExtent mip=source;unsigned skip=0;int found=0;
  for(;skip<32;++skip){if(mip.width==destination.width&&mip.height==destination.height&&mip.depth==destination.depth){found=1;break;}if(mip.width==1&&mip.height==1&&mip.depth==1)break;mip.width=half(mip.width);mip.height=half(mip.height);mip.depth=half(mip.depth);}
  out=before;int accepted=d9wg_texture_copy_plan(&source,&destination,&out);CHECK(accepted==found);
  if(found){unsigned available=skip<source.levels?source.levels-skip:0,levels=available<destination.levels?available:destination.levels;CHECK(out.source_first==skip&&out.levels==levels);}else CHECK(!memcmp(&out,&before,sizeof(out)));
 }
 D9WGTextureCopyExtent source={UINT32_MAX,1,1,32},destination={1,1,1,1};CHECK(d9wg_texture_copy_plan(&source,&destination,&out)&&out.source_first==31&&out.levels==1);
 source=(D9WGTextureCopyExtent){1,UINT32_MAX,1,32};CHECK(d9wg_texture_copy_plan(&source,&destination,&out)&&out.source_first==31&&out.levels==1);
 source=(D9WGTextureCopyExtent){1,1,UINT32_MAX,32};CHECK(d9wg_texture_copy_plan(&source,&destination,&out)&&out.source_first==31&&out.levels==1);
 source=(D9WGTextureCopyExtent){8,4,8,1};destination=(D9WGTextureCopyExtent){4,2,4,3};CHECK(d9wg_texture_copy_plan(&source,&destination,&out)&&out.source_first==1&&!out.levels);
 source.width=0;out=before;CHECK(!d9wg_texture_copy_plan(&source,&destination,&out)&&!memcmp(&out,&before,sizeof(out)));source.width=8;source.levels=33;CHECK(!d9wg_texture_copy_plan(&source,&destination,&out));source.levels=1;destination.levels=0;CHECK(!d9wg_texture_copy_plan(&source,&destination,&out));
 expected=(D9WGTextureCopyPlan){2,3};fail_phase=9;fail_at=0;CHECK(d9wg_texture_copy_levels(&expected,6,validate,prepare,copy,NULL)==0&&calls[0]==18&&calls[1]==18&&calls[2]==18);
 for(unsigned phase=0;phase<3;++phase){memset(calls,0,sizeof(calls));last_phase=0;fail_phase=phase;fail_at=5;CHECK(d9wg_texture_copy_levels(&expected,6,validate,prepare,copy,NULL)==(phase==0?1:phase==1?3:2));for(unsigned p=0;p<3;++p)CHECK(calls[p]==(p<phase?18:p==phase?5:0));}
 memset(calls,0,sizeof(calls));CHECK(d9wg_texture_copy_levels(&(D9WGTextureCopyPlan){31,0},6,NULL,NULL,NULL,NULL)==0&&!calls[0]);
 CHECK(d9wg_texture_copy_levels(&(D9WGTextureCopyPlan){31,2},6,validate,prepare,copy,NULL)==1);CHECK(d9wg_texture_copy_levels(&expected,7,validate,prepare,copy,NULL)==1);CHECK(d9wg_texture_copy_levels(&expected,1,NULL,prepare,copy,NULL)==1);
 puts("PASS shared texture copy plan: 100000 independently enumerated 3D mip matches, unequal counts/missing levels, UINT_MAX, atomic failures and six-layer validate/prepare/copy order with accepted error prefixes");return 0;}
`;fs.writeFileSync(dir+"/plan.c",code);const build=spawnSync(process.env.HOST_CC||"cc",["-std=c11","-Os","-Wall","-Wextra","-Werror","-I",root,"-I",root+"/runtime",dir+"/plan.c",process.env.D9WG_TEXTURE_COPY_RUNTIME_SOURCE||root+"/runtime/d9wg_runtime.c","-o",dir+"/plan"],{encoding:"utf8"});assert.equal(build.status,0,build.stderr);const run=spawnSync(dir+"/plan",[],{encoding:"utf8"});assert.equal(run.status,0,run.stderr);process.stdout.write(run.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
