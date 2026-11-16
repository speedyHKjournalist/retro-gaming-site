'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),source=fs.readFileSync(process.env.D9WG_RESET_MODE_FRONTEND||root+'/d3d9proxy/d3d9_proxy.c','utf8'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'d9wg-exclusive-entry-'));
function extract(name){const m=new RegExp('^static[^;{}]*\\b'+name+'\\([^;]*?\\)\\s*\\{','m').exec(source);assert(m,name);let e=source.indexOf('{',m.index)+1,d=1;for(;d;e++){if(source[e]==='{')d++;if(source[e]==='}')d--;}return source.slice(m.index,e);}
try{fs.writeFileSync(dir+'/entry.c',`#include <stdio.h>
#include <assert.h>
#include "d9wg_exclusive_window.h"
#define D3DCREATE_NOWINDOWCHANGES 0x800u
typedef struct{UINT api;struct{BOOL Windowed;}present;struct{HWND hFocusWindow;UINT BehaviorFlags;}creation;struct{UINT Width,Height;}display_mode;D9WGExclusiveWindow*exclusive_window;D9WGResetState reset_state;}D9Device;
static UINT device_compatibility_api(D9Device*d){return d->api;}
static unsigned calls,leaves,checks;static BOOL fail;static HWND focus,output;static UINT width,height,flags;static D9WGResetState*state;
BOOL d9wg_exclusive_window_enter_ex(D9WGExclusiveWindow**subscription,HWND f,HWND o,UINT w,UINT h,D9WGResetState*s,UINT mode){calls++;focus=f;output=o;width=w;height=h;state=s;flags=mode;if(fail)return FALSE;*subscription=(void*)1;return TRUE;}
void d9wg_exclusive_window_leave(D9WGExclusiveWindow**subscription){leaves++;*subscription=NULL;}
${['restore_display_mode','reset_window_mode'].map(extract).join('\n')}
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"Reset mode entry native line %u: %s\\n",__LINE__,#v);return 1;}}while(0)
int main(void){
for(UINT api=8;api<=9;api++){
for(UINT behavior=0;behavior<4;behavior++){
 D9Device d={.api=api,.creation={(HWND)11,(behavior&1?0x800u:0)|(behavior&2?0x40u:0)},.display_mode={640,480}};
 calls=leaves=0;CHECK(reset_window_mode(&d,(HWND)22)&&d.exclusive_window&&calls==1&&!leaves);
 CHECK(focus==(HWND)11&&output==(HWND)22&&width==640&&height==480&&state==&d.reset_state);
 CHECK(flags==(api==9?(2u|(behavior&1)):0u));
 d.display_mode.Width=800;d.display_mode.Height=600;CHECK(reset_window_mode(&d,(HWND)33)&&calls==2&&width==800&&height==600&&output==(HWND)33);
 fail=1;CHECK(!reset_window_mode(&d,(HWND)44)&&calls==3&&d.exclusive_window==(void*)1);fail=0;
 d.present.Windowed=TRUE;CHECK(reset_window_mode(&d,(HWND)22)&&!d.exclusive_window&&leaves==1&&calls==3);
 CHECK(reset_window_mode(&d,(HWND)22)&&!d.exclusive_window&&leaves==2&&calls==3);
 d.present.Windowed=FALSE;CHECK(reset_window_mode(&d,(HWND)22)&&d.exclusive_window);
 CHECK(restore_display_mode(&d)&&!d.exclusive_window&&leaves==3);
 CHECK(restore_display_mode(&d)&&!d.exclusive_window&&leaves==4);
}
}
printf("PASS native fullscreen entry: %u checks; actual API8/9 dispatch, focus/output identity, dimensions, NOWINDOWCHANGES, failure propagation and windowed/final detach\\n",checks);return 0;}
`);let r=spawnSync('cc',['-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+root+'/tests/native-exclusive','-I'+root+'/runtime',dir+'/entry.c','-o',dir+'/test'],{encoding:'utf8'});assert.equal(r.status,0,r.stderr);r=spawnSync(dir+'/test',[],{encoding:'utf8'});assert.equal(r.status,0,r.stdout+r.stderr);process.stdout.write(r.stdout);}finally{fs.rmSync(dir,{recursive:true,force:true});}
