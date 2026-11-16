"use strict";
const fs = require("node:fs"), path = require("node:path"), os = require("node:os"), assert = require("node:assert/strict"), {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(process.env.D7WG_LEGACY_LIGHT_SOURCE || root + "/ddrawproxy/d3d_legacy_proxy.inc", "utf8"), dir = fs.mkdtempSync(path.join(os.tmpdir(), "d7wg-legacy-light-"));
function extract(name) {const m = new RegExp("static [^;{}\\n]*?\\b" + name + "\\([^;]*?\\)\\s*\\{").exec(source); assert(m, name); let end = source.indexOf("{", m.index) + 1, depth = 1; for (; depth; end++) {if (source[end] === "{") depth++; if (source[end] === "}") depth--;} return source.slice(m.index, end);}
const code = `#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t DWORD;typedef unsigned char BYTE;typedef int BOOL,HRESULT;
typedef struct{float r,g,b,a;}Color;typedef struct{float x,y,z;}Vector;
#define LIGHT_PREFIX DWORD dwSize,dltType;Color dcvColor;Vector dvPosition,dvDirection;float dvRange,dvFalloff,dvAttenuation0,dvAttenuation1,dvAttenuation2,dvTheta,dvPhi
typedef struct{LIGHT_PREFIX;}D3DLIGHT;typedef struct{LIGHT_PREFIX;DWORD dwFlags;}D3DLIGHT2;
typedef struct{DWORD dltType;Color dcvDiffuse,dcvSpecular,dcvAmbient;Vector dvPosition,dvDirection;float dvRange,dvFalloff,dvAttenuation0,dvAttenuation1,dvAttenuation2,dvTheta,dvPhi;}D3DLIGHT7;
_Static_assert(sizeof(D3DLIGHT)==76,"DX3 SDK light");_Static_assert(sizeof(D3DLIGHT2)==80,"DX5 SDK light");_Static_assert(sizeof(D3DLIGHT7)==104,"DX7 SDK light");
typedef struct D3DLegacyViewport D3DLegacyViewport;typedef struct D3DLegacyLight D3DLegacyLight;
typedef struct{D3DLegacyViewport*legacy_current_viewport;D3DLIGHT7 light[8];BOOL enabled[8];}D3D7Device,IDirect3DDevice7;
struct D3DLegacyViewport{D3D7Device*device;D3DLegacyLight*lights;};struct D3DLegacyLight{D3DLIGHT2 light;D3DLegacyViewport*viewport;D3DLegacyLight*next_in_viewport;};typedef D3DLegacyLight IDirect3DLight;
#define WINAPI
#define D3DLIGHT_ACTIVE 1u
#define D3DLIGHT_NO_SPECULAR 2u
#define D3D7_MAX_LIGHTS 8u
#define D3D_OK 0
#define DDERR_INVALIDPARAMS (-1)
#define DDERR_GENERIC (-2)
#define TRUE 1
#define FALSE 0
#define DDWG_TRACE_ERROR(x) (x)
#define FAILED(x) ((x)<0)
#define ZeroMemory(p,n) memset(p,0,n)
#define CopyMemory(d,s,n) memcpy(d,s,n)
static unsigned checks,applies,sets,enables,fail_set;
#define MUST(x) do{checks++;if(!(x)){fprintf(stderr,"legacy light line %u: %s\\n",__LINE__,#x);exit(1);}}while(0)
static HRESULT d3d_legacy_apply_viewport_projection(D3DLegacyViewport*v){(void)v;applies++;return 0;}
static HRESULT d3d7_device_SetLight(IDirect3DDevice7*d,DWORD i,D3DLIGHT7*l){MUST(i<8);sets++;if(fail_set){fail_set--;return DDERR_GENERIC;}d->light[i]=*l;return 0;}
static HRESULT d3d7_device_LightEnable(IDirect3DDevice7*d,DWORD i,BOOL enabled){MUST(i<8);enables++;d->enabled[i]=enabled;return 0;}
${["d3d_light_from_iface", "d3d_legacy_light7", "d3d_legacy_apply_viewport", "d3d_light_SetLight", "d3d_light_GetLight"].map(extract).join("\n")}
static D3DLIGHT2 description(DWORD size,DWORD flags){D3DLIGHT2 d;memset(&d,0,sizeof(d));d.dwSize=size;d.dltType=3;d.dcvColor=(Color){0,.25f,1,1};d.dvDirection.z=1;d.dvRange=100;d.dvAttenuation0=1;d.dwFlags=flags;return d;}
static void sizes(void){D3D7Device device={0};D3DLegacyViewport viewport={&device,NULL},other={&device,NULL};D3DLegacyLight light={0};viewport.lights=&light;light.viewport=&viewport;device.legacy_current_viewport=&viewport;
 for(unsigned round=0;round<64;round++)for(DWORD size=76;size<=84;size++)for(DWORD flags=0;flags<4;flags++){
  D3DLIGHT2 full=description(size,flags);BYTE*bytes=malloc(size),*before=malloc(size);MUST(bytes&&before);memset(bytes,0xa5,size);memcpy(bytes,&full,size<80?size:80);memcpy(before,bytes,size);DWORD effective=size<80?D3DLIGHT_ACTIVE:flags;unsigned a=applies,s=sets,e=enables;MUST(!d3d_light_SetLight(&light,(D3DLIGHT*)bytes));MUST(!memcmp(bytes,before,size));MUST(light.light.dwFlags==effective&&light.light.dwSize==80&&applies==a+1&&sets==s+1&&enables==e+8);MUST(device.enabled[0]==!!(effective&D3DLIGHT_ACTIVE));for(unsigned i=1;i<8;i++)MUST(!device.enabled[i]);MUST(!memcmp(&device.light[0].dcvDiffuse,&full.dcvColor,sizeof(Color)));Color zero={0};MUST(!memcmp(&device.light[0].dcvSpecular,effective&D3DLIGHT_NO_SPECULAR?&zero:&full.dcvColor,sizeof(Color)));MUST(!memcmp(&device.light[0].dcvAmbient,&zero,sizeof(Color)));MUST(!memcmp(&device.light[0].dvDirection,&full.dvDirection,sizeof(Vector)));
  struct{D3DLIGHT2 light;DWORD guard[2];}got;memset(&got,0x5a,sizeof(got));got.light.dwSize=80;MUST(!d3d_light_GetLight(&light,(D3DLIGHT*)&got.light)&&got.light.dwFlags==effective&&got.guard[0]==0x5a5a5a5a&&got.guard[1]==0x5a5a5a5a);free(before);free(bytes);
 }
 device.legacy_current_viewport=&other;D3DLIGHT2 input=description(76,0),old=light.light;unsigned a=applies;MUST(!d3d_light_SetLight(&light,(D3DLIGHT*)&input)&&light.light.dwFlags==1&&applies==a);light.viewport=NULL;MUST(!d3d_light_SetLight(&light,(D3DLIGHT*)&input)&&applies==a);light.viewport=&viewport;device.legacy_current_viewport=&viewport;fail_set=1;input.dcvColor.r=.5f;MUST(d3d_light_SetLight(&light,(D3DLIGHT*)&input)==DDERR_GENERIC&&light.light.dwFlags==1&&light.light.dcvColor.r==.5f);MUST(!d3d_light_SetLight(&light,(D3DLIGHT*)&input)&&device.enabled[0]);old=light.light;for(unsigned size=0;size<76;size++){input.dwSize=size;unsigned s=sets;MUST(d3d_light_SetLight(&light,(D3DLIGHT*)&input)==DDERR_INVALIDPARAMS&&sets==s&&!memcmp(&old,&light.light,sizeof(old)));}MUST(d3d_light_SetLight(&light,NULL)==DDERR_INVALIDPARAMS&&!memcmp(&old,&light.light,sizeof(old)));}
int main(void){sizes();printf("PASS actual legacy light C: %u checks, 2304 size/flag transitions, exact DX3 guarded allocations, full D3DLIGHT2 inactive/NO_SPECULAR semantics, current/detached viewport isolation, cache/query guards and accepted update failure prefixes\\n",checks);return 0;}
`;
try {const file = dir + "/light.c", binary = dir + "/light"; fs.writeFileSync(file, code); const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c11", "-Os", "-Wall", "-Wextra", "-Werror", file, "-o", binary], {encoding: "utf8"}); assert.equal(built.status, 0, built.stderr); const run = spawnSync(binary, [], {encoding: "utf8"}); assert.equal(run.status, 0, run.stderr); process.stdout.write(run.stdout);} finally {fs.rmSync(dir, {recursive: true, force: true});}
