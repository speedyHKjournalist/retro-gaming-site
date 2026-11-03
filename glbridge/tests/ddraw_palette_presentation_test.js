"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const source = fs.readFileSync(process.env.DDRAW_PALETTE_PRESENT_SOURCE || path.join(__dirname, "../ddrawproxy/ddraw_proxy.c"), "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "ddraw-palette-present-"));
function extract(name) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} return source.slice(m.index, end);}
const code = `#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t DWORD;typedef int32_t HRESULT;typedef int BOOL;typedef struct{unsigned char peRed,peGreen,peBlue,peFlags;} PALETTEENTRY,*LPPALETTEENTRY;
typedef struct DDPalette DDPalette;typedef struct{BOOL indexed;DDPalette *palette;} DDSurface;typedef struct{DDSurface*primary;} Owner;
struct DDPalette{Owner*owner;PALETTEENTRY entries[256];};typedef DDPalette IDirectDrawPalette;
#define WINAPI
#define DD_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_GENERIC (-2)
#define DDWG_TRACE_ERROR(x) (x)
static unsigned checks,tables,blits,presents,fail_blit,fail_present,sequence;static Owner*expected_owner;static DDSurface*expected_primary;
static DDPalette *palette_from_iface(IDirectDrawPalette*p){return p;}
static void emit_palette_table(DDPalette*p){(void)p;tables++;sequence=1;}
static BOOL emit_blt_to_screen(DDSurface*s,const void*r){if(sequence!=1||s!=expected_primary||r)return 0;sequence=2;blits++;if(fail_blit){fail_blit=0;return 0;}return 1;}
static BOOL emit_present_and_flush(Owner*o){if(sequence!=2||o!=expected_owner)return 0;sequence=3;presents++;if(fail_present){fail_present=0;return 0;}return 1;}
${extract("palette_SetEntries")}
${extract("palette_GetEntries")}
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"DDraw palette line %u: %s\\n",__LINE__,#x);return 1;}}while(0)
int main(void){(void)&emit_blt_to_screen;(void)&emit_present_and_flush;
 for(unsigned profile=0;profile<5;profile++)for(unsigned start=0;start<=256;start+=16)for(unsigned count=0;count<=256;count+=16){
  DDPalette p={0},other={0};DDSurface front={.indexed=1,.palette=&p};Owner owner={.primary=&front};p.owner=&owner;if(profile==0)p.owner=NULL;if(profile==1)owner.primary=NULL;if(profile==2)front.indexed=0;if(profile==3)front.palette=&other;expected_owner=&owner;expected_primary=&front;
  PALETTEENTRY entries[256],before[256],input[256];for(unsigned i=0;i<256;i++)p.entries[i]=(PALETTEENTRY){1,2,3,4};for(unsigned i=0;i<256;i++)entries[i]=(PALETTEENTRY){(unsigned char)i,5,6,7};memcpy(before,p.entries,sizeof(before));memcpy(input,entries,sizeof(input));unsigned t=tables,b=blits,n=presents;BOOL valid=count<=256-start;
  CHECK(palette_SetEntries(&p,0,start,count,entries)==(valid?0:DDERR_INVALIDPARAMS));CHECK(!memcmp(input,entries,sizeof(input)));CHECK(tables==t+valid&&blits==b+(valid&&profile==4)&&presents==n+(valid&&profile==4));
  for(unsigned i=0;i<256;i++)CHECK(!memcmp(&p.entries[i],valid&&i>=start&&i<start+count?&entries[i-start]:&before[i],sizeof(PALETTEENTRY)));
  PALETTEENTRY got[256];CHECK(!palette_GetEntries(&p,0,0,256,got)&&!memcmp(got,p.entries,sizeof(got)));
 }
 DDPalette p={0};DDSurface front={.indexed=1,.palette=&p};Owner owner={&front};p.owner=&owner;expected_owner=&owner;expected_primary=&front;PALETTEENTRY input={10,20,30,40};unsigned t=tables,b=blits,n=presents;
 CHECK(palette_SetEntries(&p,0,0,1,NULL)==DDERR_INVALIDPARAMS&&tables==t&&blits==b&&presents==n);CHECK(palette_SetEntries(&p,0,0xffffffffu,1,&input)==DDERR_INVALIDPARAMS&&tables==t);
 fail_blit=1;CHECK(palette_SetEntries(&p,0,1,1,&input)==DDERR_GENERIC&&p.entries[1].peRed==10&&tables==t+1&&blits==b+1&&presents==n);CHECK(!palette_SetEntries(&p,0,1,1,&input)&&presents==n+1);
 fail_present=1;CHECK(palette_SetEntries(&p,0,1,1,&input)==DDERR_GENERIC);CHECK(!palette_SetEntries(&p,0,1,1,&input));
 printf("PASS actual DDraw palette presentation: %u checks, 1445 range/profile cases, immediate table/resolve/Present ordering, no unrelated presentation, input preservation and failed-present retries\\n",checks);return 0;
}
`;
try {const file = dir + "/palette.c", binary = dir + "/palette"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", file, "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
