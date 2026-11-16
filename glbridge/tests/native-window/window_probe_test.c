#include "window_mock.h"
#include "runtime/d9wg_window_probe.h"
#include "window_region.h"
#include <stdio.h>
#include <string.h>
#define CHECK(v) do{if(!(v)){fprintf(stderr,"window probe line %u: %s\n",__LINE__,#v);return 1;}}while(0)
static D9WGWindowProbe p;static D9WGWindowGeometry g;static D9WGWindowStateWithRegion r;
static void capture(void){d9wg_window_probe_capture(1,7,&p);}
static void geometry(unsigned api){d9wg_window_probe_geometry(&p,api,640,480,&g);}
int main(void){
 window_mock_reset();capture();CHECK(p.state.flags==(D9WG_WINDOW_IS_WINDOW|D9WG_WINDOW_VISIBLE|D9WG_WINDOW_FOREGROUND));CHECK(p.state.window_x==20&&p.state.window_y==30&&p.state.client_width==160&&p.state.client_height==100&&p.state.window_width==176&&p.state.window_height==138);CHECK(p.region.rect_count==0);CHECK(calls.client==1&&calls.origin==1&&calls.outer==1&&calls.foreground==1&&calls.valid==1&&calls.visible==1&&calls.iconic==1&&calls.dc==1&&calls.release==1&&calls.create==2&&calls.deleted==2);
 for(unsigned i=0;i<3;++i){geometry(i);CHECK(g.x==20&&g.y==30&&g.width==160&&g.height==100);}
 d9wg_window_probe_report(&p,41,1,0,&r);CHECK(r.state.device_handle==41&&(r.state.flags&D9WG_WINDOW_FULLSCREEN)&&r.state.window_width==176);
 d9wg_window_probe_report(&p,41,0,D9WG_WINDOW_REPORT_CLIENT_SIZE,&r);CHECK(r.state.window_width==160&&r.state.window_height==100);
 d9wg_window_probe_report(&p,41,1,D9WG_WINDOW_REPORT_NO_SURFACE,&r);CHECK(r.state.window_x==12&&r.state.window_y==0&&(r.state.flags&D9WG_WINDOW_NO_SURFACE)&&!(r.state.flags&(D9WG_WINDOW_VISIBLE|D9WG_WINDOW_REGION|D9WG_WINDOW_OCCLUDED)));
 d9wg_window_probe_report(&p,41,1,D9WG_WINDOW_REPORT_ZERO_ORIGIN,&r);CHECK(!r.state.window_x&&!r.state.window_y&&r.state.client_width==160);
 window_mock_reset();fixture.region_count=0;capture();CHECK(p.state.flags&D9WG_WINDOW_OCCLUDED);CHECK(!(p.state.flags&D9WG_WINDOW_REGION));
 window_mock_reset();fixture.screen_rects[0]=(RECT){0,0,100,100};capture();CHECK((p.state.flags&D9WG_WINDOW_REGION)&&p.region.rect_count==1);CHECK(p.region.rects[0][0]==0&&p.region.rects[0][1]==0&&p.region.rects[0][2]==80&&p.region.rects[0][3]==70);
 window_mock_reset();fixture.origin=(POINT){-200,-300};fixture.screen_rects[0]=(RECT){-190,-280,-100,-250};capture();CHECK(p.region.rect_count==1&&p.region.rects[0][0]==10&&p.region.rects[0][1]==20&&p.region.rects[0][2]==100&&p.region.rects[0][3]==50);
 for(unsigned n=2;n<=33;++n){window_mock_reset();fixture.region_count=n;for(unsigned i=0;i<n;++i)fixture.screen_rects[i]=(RECT){20+(int)i*4,30,22+(int)i*4,130};capture();CHECK(calls.data==(n<=32?2u:1u));CHECK(p.region.rect_count==(n<=32?n:0));CHECK(!!(p.state.flags&D9WG_WINDOW_REGION)==(n<=32));if(n<=32){CHECK(p.region.rects[n-1][0]==(int32_t)(n-1)*4&&p.region.rects[n-1][2]==(int32_t)(n-1)*4+2);d9wg_window_probe_report(&p,41,0,0,&r);CHECK(r.region.rect_count==n&&r.region.rects[n-1][2]==(int32_t)(n-1)*4+2);}}
 for(unsigned f=FAIL_DC;f<=FAIL_DATA_READ;f*=2){window_mock_reset();fixture.failures=f;fixture.region_count=2;fixture.screen_rects[1]=(RECT){100,30,180,130};capture();CHECK(!(p.state.flags&(D9WG_WINDOW_REGION|D9WG_WINDOW_OCCLUDED)));CHECK(calls.create==calls.deleted||f==FAIL_CREATE);}
 for(unsigned f=FAIL_CREATE;f<=FAIL_RANDOM;f*=2){window_mock_reset();fixture.failures=f;fixture.clip_kind=NULLREGION;capture();CHECK(p.state.flags&D9WG_WINDOW_OCCLUDED);CHECK(calls.clip==1);}
 window_mock_reset();fixture.non_nt=1;fixture.clip_kind=NULLREGION;capture();CHECK((p.state.flags&D9WG_WINDOW_OCCLUDED)&&calls.clip==1&&!calls.random&&!calls.create);
 for(unsigned field=0;field<3;++field){window_mock_reset();if(field==0)fixture.visible=0;if(field==1)fixture.iconic=1;if(field==2)fixture.client=(RECT){0,0,0,0};capture();CHECK(!calls.dc&&!p.region.rect_count&&!(p.state.flags&D9WG_WINDOW_OCCLUDED));}
 window_mock_reset();fixture.failures=FAIL_ORIGIN;capture();CHECK(!calls.dc&&!p.origin_succeeded&&!p.state.window_x&&!p.state.window_y);geometry(1);CHECK(!g.width&&!g.height);geometry(0);CHECK(g.width==160&&g.height==100&&!g.x);geometry(2);CHECK(g.width==160&&g.height==100&&!g.x);
 V86WindowRegion legacy;CHECK(v86_window_visible_region(1,&legacy)==V86_WINDOW_REGION_ALL&&!legacy.count&&!calls.dc);
 window_mock_reset();fixture.failures=FAIL_CLIENT;capture();CHECK(!calls.dc&&!p.client_succeeded);geometry(0);CHECK(g.x==20&&g.y==30&&g.width==640&&g.height==480);geometry(1);CHECK(!g.width&&!g.height&&!g.x);geometry(2);CHECK(!g.x&&!g.y&&g.width==640&&g.height==480);
 window_mock_reset();fixture.client=(RECT){0,0,0,20};capture();geometry(0);CHECK(g.x==12&&g.y==0&&g.width==176&&g.height==138);geometry(1);CHECK(g.x==20&&g.y==30&&!g.width&&g.height==20);geometry(2);CHECK(g.x==20&&g.y==30&&g.width==640&&g.height==20);
 fixture.failures=FAIL_OUTER;capture();geometry(0);CHECK(g.x==20&&g.y==30&&!g.width&&g.height==20);
 window_mock_reset();fixture.iconic=1;capture();geometry(1);CHECK(!g.width&&!g.height&&!g.x);d9wg_window_probe_report(&p,41,0,D9WG_WINDOW_REPORT_CLIENT_SIZE,&r);CHECK(!r.state.window_x&&!r.state.window_width&&(r.state.flags&D9WG_WINDOW_ICONIC));
 window_mock_reset();fixture.valid=0;capture();CHECK(!p.state.flags&&!calls.client&&!calls.origin&&!calls.outer&&!calls.dc&&calls.foreground==1);geometry(0);CHECK(g.width==640&&g.height==480);geometry(1);CHECK(!g.width&&!g.height);
 window_mock_reset();d9wg_window_probe_capture(1,0,&p);CHECK(calls.client==1&&calls.origin==1&&!calls.outer&&!calls.foreground&&!calls.visible&&!calls.dc);CHECK(p.state.client_width==160);
 window_mock_reset();d9wg_window_probe_capture(1,D9WG_WINDOW_PROBE_OUTER_IF_EMPTY,&p);CHECK(!calls.outer);fixture.client.right=0;d9wg_window_probe_capture(1,D9WG_WINDOW_PROBE_OUTER_IF_EMPTY,&p);CHECK(calls.outer==1);
 window_mock_reset();d9wg_window_probe_capture(1,D9WG_WINDOW_PROBE_STATE|D9WG_WINDOW_PROBE_IDENTITY_ONLY,&p);CHECK(calls.foreground==1&&!calls.valid&&!calls.client&&!calls.dc);d9wg_window_probe_report(&p,41,1,D9WG_WINDOW_REPORT_CLIENT_SIZE|D9WG_WINDOW_REPORT_HIDDEN,&r);CHECK(!r.state.flags&&!r.state.window_x&&!r.state.client_width&&r.state.hwnd==1&&r.state.foreground_hwnd==1&&!r.region.rect_count);
 window_mock_reset();fixture.visible=0;fixture.client=(RECT){INT32_MIN,INT32_MIN,INT32_MAX,INT32_MAX};fixture.outer=fixture.client;capture();CHECK(p.state.client_width==UINT32_MAX&&p.state.window_width==UINT32_MAX);
 puts("PASS actual Win32 window capture: single-query facts, GDI full/none/partial/2..32/overflow regions, failures, negative coordinates, three geometry/report policies and lightweight/hidden modes");return 0;
}
