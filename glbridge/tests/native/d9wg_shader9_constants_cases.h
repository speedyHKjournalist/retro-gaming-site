#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "d9wg_shader9_constants.h"
static uint32_t reg(unsigned type,unsigned index){return 0x80e40000u|((type&7u)<<28)|((type&24u)<<8)|index;}
int main(void){unsigned checks=0;const unsigned types[]={2,11,12,13,7,14};for(unsigned pixel=0;pixel<2;pixel++)for(unsigned major=1;major<=3;major++)for(unsigned minor=0;minor<=1;minor++)for(unsigned sw=0;sw<=1;sw++)for(unsigned t=0;t<6;t++)for(unsigned idx=0;idx<2048;idx++){
 unsigned type=types[t],index=idx+(type>=11&&type<=13?(type-10)*2048:0);unsigned limit=type==7||type==14?(!pixel&&sw?2048:16):pixel?(major==1?8:major==2?32:224):sw?8192:256;int wanted=index<limit&&(!(type==7||type==14)||!pixel||major>=3||(major==2&&minor));
 uint32_t code[]={(pixel?0xffff0000u:0xfffe0000u)|(major<<8)|minor,major==1?1u:0x02000001u,0x800f0000u,reg(type,idx),0xffffu};assert(d9wg_shader9_constants_valid(code,5,sw)==wanted);checks++;
 uint32_t defined[]={code[0],type==7?0x05000030u:type==14?0x0200002fu:0x05000051u,reg(type,idx),0xffffu,0xffffffffu,0xa00fffffu,0xe00f0fffu,0xffffu};unsigned length=type==14?5:8;if(type==14)defined[4]=0xffffu;assert(d9wg_shader9_constants_valid(defined,length,sw)==wanted);checks++;
}
uint32_t payload[]={0xffff0300u,0x0003fffeu,0xffffffffu,0xa0e407ffu,0xe0e40fffu,0x0200001fu,0xa0e407ffu,0x900f0000u,0xffffu};assert(d9wg_shader9_constants_valid(payload,9,0));checks++;
for(unsigned n=0;n<9;n++){assert(!d9wg_shader9_constants_valid(payload,n,0));checks++;}
assert(!d9wg_shader9_constants_valid(NULL,9,0));checks++;
printf("PASS shader9 constant operands: %u checks; register banks/bounds, all profiles, software mode, immediate/comment/semantic isolation and truncation\n",checks);return 0;}
