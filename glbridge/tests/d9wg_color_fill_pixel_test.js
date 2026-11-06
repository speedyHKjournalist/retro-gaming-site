'use strict';
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-fill-pixel-')),root=path.resolve(__dirname,'..');
try{
fs.writeFileSync(dir+'/test.c',`#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "${root}/runtime/d9wg_color_fill_pixel.h"
/* Independent nearest representable binary16 oracle. Eight-bit UNORM is
 * nonnegative and at most one, so search the complete finite range [0,1]. */
static unsigned half(double v){double best=2;unsigned result=0;for(unsigned h=0;h<=0x3c00;h++){
 double x=(h>>10)?ldexp(1+(h&1023)/1024.0,(int)(h>>10)-15):ldexp(h,-24);
 double d=fabs(v-x);if(d<best||(d==best&&!(h&1))){best=d;result=h;}}
 return result;}
int main(void){unsigned checks=0;unsigned char pixel[16];unsigned formats[]={21,22,23,34,36,111,112,113,114,115,116};
for(unsigned x=0;x<256;x++)for(unsigned f=0;f<sizeof(formats)/sizeof(*formats);f++){
 unsigned r=x,g=255-x,b=(x*37)&255,a=(x*13)&255,fmt=formats[f];uint32_t c=(a<<24)|(r<<16)|(g<<8)|b;
 unsigned n=d9wg_color_fill_pixel(fmt,c,pixel);assert(n&&n<=16);uint16_t w[8];float v[4];uint32_t p;memcpy(w,pixel,n);memcpy(v,pixel,n);memcpy(&p,pixel,4);
 if(fmt==21||fmt==22)assert(p==(fmt==21?c:c|0xff000000u));
 if(fmt==23)assert(w[0]==((r/8)*2048+(g/4)*32+b/8));
 if(fmt==34||fmt==36){assert(w[0]==r*257&&w[1]==g*257);if(fmt==36)assert(w[2]==b*257&&w[3]==a*257);}
 if(fmt>=111&&fmt<=113){unsigned values[]={r,g,b,a};for(unsigned j=0;j<n/2;j++)assert(w[j]==half(values[j]/255.0));}
 if(fmt>=114){unsigned values[]={r,g,b,a};for(unsigned j=0;j<n/4;j++)assert(fabs(v[j]-values[j]/255.0)<1e-7);}
 checks++;
}
assert(!d9wg_color_fill_pixel(0,0,pixel));printf("PASS ColorFill native pixel packing: %u format/channel combinations; exact UNORM, RGB565 and independent float/half oracle\\n",checks);
}
`);
const cc=spawnSync('cc',['-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',dir+'/test.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(cc.status,0,cc.stderr);
const run=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(run.status,0,run.stdout+run.stderr);process.stdout.write(run.stdout);
}finally{fs.rmSync(dir,{recursive:true,force:true});}
