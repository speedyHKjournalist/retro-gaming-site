struct Input{float p[3],n[3];DWORD diffuse,specular;};
struct Output{float p[4];DWORD diffuse,specular;};
static void seed(D3D7Device*d,unsigned api){
 memset(d,0,sizeof(*d));d->creation_version=api;d->legacy_material=api<7?(void*)1:NULL;d->wire_handle=9;d->viewport=(D3DVIEWPORT7){0,0,128,128,0,1};
 for(unsigned m=0;m<24;m++)for(unsigned i=0;i<16;i++)d->transforms[m].v[i]=i%5==0?1:0;
 d->render_states[137]=d->render_states[141]=d->render_states[142]=1;d->render_states[145]=1;d->render_states[146]=2;d->render_states[29]=1;d->light_set[0]=d->light_enabled[0]=1;
}
static void light(D3D7Device*d,unsigned kind){D3DLIGHT7*l=&d->lights[0];memset(l,0,sizeof(*l));l->dltType=kind;l->dcvSpecular=(Color){1,1,1,0};l->dvDirection.z=1;l->dvRange=100;l->dvAttenuation2=1;l->dvFalloff=1;l->dvTheta=.261799388f;l->dvPhi=1.04719755f;}
static BOOL close_color(DWORD a,DWORD b){if((a>>24)!=(b>>24))return 0;for(unsigned i=0;i<3;i++){int delta=(int)((a>>(8*i))&255)-(int)((b>>(8*i))&255);if(delta < -1||delta>1)return 0;}return 1;}
static void process_case(D3D7Device*d,const char*phase,unsigned kind,unsigned power,unsigned colors,unsigned cv,unsigned source){
 BYTE inputs[3+11*32],storage[7+12*24+13];memset(inputs,0xa5,sizeof(inputs));memset(storage,0x6d,sizeof(storage));UINT stride=colors?32:24;
 for(unsigned i=0;i<9;i++){struct Input v={{((int)(i%3)-1)*.5f,((int)(i/3)-1)*.5f,1},{0,0,-1},0x40ffffff,0xc0ffffff};memcpy(inputs+3+(i+1)*stride,&v,stride);}
 BYTE before[sizeof(inputs)];memcpy(before,inputs,sizeof(inputs));D3D7VertexBuffer dst={.desc={16,0,0xc4,12},.shadow=storage+7,.stride=24,.byte_count=12*24,.handle=1};
 unsigned prior_upload=uploads;MUST(!d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,2,9,inputs+3,0x12|(colors?0xc0:0),stride,1,d,0));MUST(uploads==prior_upload+1&&uploaded_offset==48&&uploaded_bytes==9*24);MUST(!memcmp(inputs,before,sizeof(inputs)));
 for(unsigned i=0;i<7+48;i++)MUST(storage[i]==0x6d);for(unsigned i=7+11*24;i<sizeof(storage);i++)MUST(storage[i]==0x6d);
 unsigned matched=0;for(unsigned i=0;i<sizeof(rows)/sizeof(*rows);i++){const struct Row*r=&rows[i];if(r->api!=d->creation_version||strcmp(r->phase,phase)||r->kind!=kind||r->power!=power||r->colors!=colors||r->cv!=cv||r->source!=source)continue;
  struct Output out;memcpy(&out,dst.shadow+48+r->vertex*24,24);MUST(close_color(out.diffuse,r->diffuse));
  /* Positive-power old point/spot approximation remains an explicit Wine
   * failure, outside this zero-power/material-alpha regression. */
  if(r->api==7||kind==3||power==0||power==2||power==4){if(!close_color(out.specular,r->specular))fprintf(stderr,"native mismatch api%u %s kind%u p%u colors%u cv%u src%u vertex%u got%08x wanted%08x\n",r->api,phase,kind,power,colors,cv,source,r->vertex,out.specular,r->specular);MUST(close_color(out.specular,r->specular));}matched++;
 }MUST(matched==9);
}
static void material_case(D3D7Device*d,D3DMATERIAL7*m){unsigned sent=emits;MUST(!d3d7_device_SetMaterial(d,m)&&emits==sent+1&&!memcmp(&d->material,m,sizeof(*m)));D3DMATERIAL7 got;MUST(!d3d7_device_GetMaterial(d,&got)&&!memcmp(&got,m,sizeof(got)));MUST(wire_material.power==m->power);for(unsigned i=0;i<3;i++)MUST(wire_material.specular[i]==(d->creation_version<7&&m->power==0?0:1));}
static void native_cases(void){
 static const float powers[]={0,30,0,5,0};for(unsigned round=0;round<4;round++)for(unsigned api=3;api<=7;api+=4){D3D7Device d;seed(&d,api);D3DMATERIAL7 m={.specular={1,1,1,.5f}};
 for(unsigned kind=1;kind<=3;kind++){light(&d,kind);for(unsigned p=0;p<5;p++){m.power=powers[p];material_case(&d,&m);process_case(&d,"sequence",kind,p,0,api==7,api==7?2:0);}}
 if(api==3)continue;
 m.power=30;material_case(&d,&m);m.power=0;material_case(&d,&m);process_case(&d,"set_without_process",3,0,0,1,2);
 for(unsigned colors=0;colors<2;colors++)for(unsigned cv=0;cv<2;cv++)for(unsigned source=0;source<3;source++){d.render_states[141]=cv;d.render_states[146]=source;process_case(&d,"source",3,0,colors,cv,source);}
 d.render_states[141]=0;process_case(&d,"new_destination",3,0,0,0,2);
 D3D7StateBlock b={0};D3DMATERIAL7 before=d.material;m.power=17;d.recording_state_block=&b;unsigned sent=emits;MUST(!d3d7_device_SetMaterial(&d,&m)&&b.material_mask&&!memcmp(&b.material,&m,sizeof(m))&&emits==sent&&!memcmp(&d.material,&before,sizeof(before)));process_case(&d,"record_during",3,0,0,0,2);d.recording_state_block=NULL;process_case(&d,"record_end",3,0,0,0,2);material_case(&d,&b.material);process_case(&d,"record_apply",3,17,0,0,2);m.power=0;material_case(&d,&m);process_case(&d,"record_zero",3,0,0,0,2);
 }
}
static void isolation_cases(void){
 D3D7Device d;seed(&d,7);light(&d,3);D3DMATERIAL7 m={.specular={1,1,1,.5f},.power=30};D3D7Device before=d;fail_emit=1;MUST(d3d7_device_SetMaterial(&d,&m)==D3DERR_MATERIAL_SETDATA_FAILED&&!memcmp(&d,&before,sizeof(d)));fail_emit=0;MUST(d3d7_device_SetMaterial(&d,NULL)==DDERR_INVALIDPARAMS);MUST(d3d7_device_GetMaterial(&d,NULL)==DDERR_INVALIDPARAMS);material_case(&d,&m);
 struct Input in={{0,0,1},{0,0,-1},0x40ffffff,0xc0ffffff};BYTE output[24];memset(output,0x6d,24);D3D7VertexBuffer dst={.desc={16,0,0xc4,1},.shadow=output,.stride=24,.byte_count=24,.handle=1};
 unsigned sent=uploads;MUST(d3d7_process_vertex_bytes(&dst,0,0,1,(BYTE*)&in,0xd2,32,0,&d,0)==D3DERR_INVALIDVERTEXFORMAT);MUST(d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,0,(BYTE*)&in,0xd2,32,0,&d,0)==D3DERR_INVALIDVERTEXFORMAT);dst.locked=1;MUST(d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,1,(BYTE*)&in,0xd2,32,0,&d,0)==D3DERR_INVALIDVERTEXFORMAT);dst.locked=0;MUST(d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,1,1,(BYTE*)&in,0xd2,32,0,&d,0)==DDERR_INVALIDPARAMS);MUST(uploads==sent);for(unsigned i=0;i<24;i++)MUST(output[i]==0x6d);
 d.render_states[141]=0;d.render_states[140]=0;d.render_states[28]=0;d.render_states[140]=3;float end=4;memcpy(&d.render_states[37],&end,4);MUST(!d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,1,(BYTE*)&in,0xd2,32,0,&d,D3DPV_DONOTCOPYDATA));DWORD s;memcpy(&s,output+20,4);MUST((s>>24)==191);d.render_states[140]=0;
 MUST(!d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM,0,1,(BYTE*)&in,0xd2,32,0,&d,0));memcpy(&s,output+20,4);MUST(s==in.specular);memset(output,0x6d,24);MUST(!d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM,0,1,(BYTE*)&in,0xd2,32,0,&d,D3DPV_DONOTCOPYDATA));memcpy(&s,output+20,4);MUST(s==0x6d6d6d6d);
 D9WGTNLLighting state;memset(&state,0xa5,sizeof(state));D9WGTNLLightingFlags flags={.lighting=1};D9WGSetMaterial mat={.specular={1,1,1,.5f}};d9wg_lighting_init(&state,&mat,0,&flags);MUST(!state.specular_alpha_material);MUST(!state.position_matrix_override);float pos[]={0,0,1,1},normal[]={0,0,-1},c1[]={1,1,1,.75f};DWORD diffuse,specular;d9wg_lighting_vertex_view_float(&state,pos,normal,NULL,NULL,&diffuse,&specular);MUST(!(specular>>24));d9wg_lighting_vertex_view_float(&state,pos,normal,NULL,c1,&diffuse,&specular);MUST((specular>>24)==191);
}

static void absent_material_cases(void){
 for(unsigned bound=0;bound<2;bound++)for(unsigned normal=0;normal<2;normal++)for(unsigned colors=0;colors<2;colors++)for(unsigned lit=0;lit<2;lit++)for(unsigned keep=0;keep<2;keep++){
  D3D7Device d;seed(&d,3);d.light_set[0]=d.light_enabled[0]=0;d.legacy_material=bound?(void*)1:NULL;D3DMATERIAL7 m={.emissive={0,1,0,0}};MUST(!d3d7_device_SetMaterial(&d,&m));DWORD fvf=0x102|(normal?0x10:0)|(colors?0xc0:0);D9WGProcessLayout layout;MUST(d9wg_process_fvf(7,fvf,&layout));BYTE input[40]={0},out[32];float pos[]={0,0,.5f},n[]={0,0,-1};DWORD c0=0x40ffffff,c1=0xc0123456;memcpy(input,pos,12);if(normal)memcpy(input+layout.normal,n,12);if(colors){memcpy(input+layout.diffuse,&c0,4);memcpy(input+layout.specular,&c1,4);}float uv[2]={.25f,.75f};memcpy(input+layout.texcoord[0],uv,8);memset(out,0x6d,32);D3D7VertexBuffer dst={.desc={16,0,0x1c4,1},.shadow=out,.stride=32,.byte_count=32,.handle=1};MUST(!d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM|(lit?D3DVOP_LIGHT:0),0,1,input,fvf,layout.stride,0,&d,keep?D3DPV_DONOTCOPYDATA:0));DWORD diffuse,specular;memcpy(&diffuse,out+16,4);memcpy(&specular,out+20,4);MUST(!memcmp(out+24,uv,8));
  if(lit&&bound){MUST(diffuse==((colors?0x40000000:0)|0x0000ff00));MUST(specular==(colors?0xc0000000:0));}else{MUST(diffuse==(colors?c0:0xffffffff));MUST(specular==(colors?c1:0));}
 }
}

static void normal_seed(D3D7Device*d,unsigned api,unsigned normalize){seed(d,api);light(d,3);d->lights[0].dcvDiffuse=(Color){1,1,1,0};d->render_states[29]=d->render_states[141]=0;d->render_states[143]=normalize;D3DMATERIAL7 material={.diffuse={0,0,1,1},.power=30};MUST(!d3d7_device_SetMaterial(d,&material));}
static void normal_process(D3D7Device*d,unsigned kind,BOOL history,unsigned normalize,unsigned sequence,unsigned step){
 struct MatrixVertex input[4];normal_vertices_for(kind,input);if(history)for(unsigned i=0;i<4;i++){input[i].n[0]=1;input[i].n[1]=input[i].n[2]=0;}memcpy(&d->transforms[1],normal_worlds[kind],64);memcpy(&d->transforms[2],normal_worlds[0],64);if(!history&&kind==6){d->transforms[2].v[0]=3;d->transforms[2].v[5]=.5f;d->transforms[2].v[10]=2;}if(!history&&kind==12){d->transforms[2].v[2]=.3f;d->transforms[2].v[4]=.25f;d->transforms[2].v[9]=.5f;}
 struct Output out[4];memset(out,0x6d,sizeof(out));D3D7VertexBuffer dst={.desc={16,0,0xc4,4},.shadow=(BYTE*)out,.stride=24,.byte_count=sizeof(out),.handle=1};MUST(!d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM|D3DVOP_LIGHT,0,4,(BYTE*)input,0x12,24,0,d,0));unsigned matches=0;
 if(history){for(unsigned i=0;i<sizeof(normal_history)/sizeof(*normal_history);i++){const struct HistoryRow*r=&normal_history[i];if(r->normalize==normalize&&r->sequence==sequence&&r->step==step){if(!close_color(out[0].diffuse,r->diffuse))fprintf(stderr,"normal history norm%u seq%u step%u actual%08x expected%08x\n",normalize,sequence,step,out[0].diffuse,r->diffuse);MUST(close_color(out[0].diffuse,r->diffuse));matches++;}}MUST(matches==1);}
 else{for(unsigned i=0;i<sizeof(normal_rows)/sizeof(*normal_rows);i++){const struct NormalRow*r=&normal_rows[i];if(r->api==d->creation_version&&r->matrix==kind&&r->normalize==normalize){if(!close_color(out[r->vertex].diffuse,r->diffuse))fprintf(stderr,"normal matrix api%u kind%u norm%u actual%08x expected%08x\n",r->api,kind,normalize,out[r->vertex].diffuse,r->diffuse);MUST(close_color(out[r->vertex].diffuse,r->diffuse)&&out[r->vertex].specular==r->specular);matches++;}}MUST(matches==4);}
}
static void normal_cases(void){for(unsigned api=3;api<=7;api+=4){D3D7Device d;normal_seed(&d,api,0);for(unsigned normalize=0;normalize<2;normalize++){d.render_states[143]=normalize;for(unsigned kind=0;kind<13;kind++)normal_process(&d,kind,0,normalize,0,0);}}
 for(unsigned normalize=0;normalize<2;normalize++)for(unsigned sequence=0;sequence<3;sequence++){D3D7Device d;normal_seed(&d,7,normalize);for(unsigned step=0;step<3;step++){unsigned kind=step==0?5:step==2?1:sequence==0?0:sequence==1?2:4;normal_process(&d,kind,1,normalize,sequence,step);}}
}
static void native_viewport_vertices(void){
 for(unsigned v=0;v<11;++v){if(v==6||v==7||v==10)continue;D3D7Device d;seed(&d,7);memcpy(&d.viewport,native_viewports[v],24);const float input[4][3]={{0,0,0},{1,1,1},{-1,-1,.5f},{.5f,-.5f,.25f}};BYTE output[64];memset(output,0x6d,64);D3D7VertexBuffer dst={.desc={16,0,4,4},.shadow=output,.stride=16,.byte_count=64,.handle=1};MUST(!d3d7_process_vertex_bytes(&dst,D3DVOP_TRANSFORM,0,4,(const BYTE*)input,2,12,0,&d,0));unsigned matched=0;for(unsigned i=0;i<32;++i){const struct ViewportVertexRow*r=&viewport_vertex_rows[i];if(r->viewport!=v)continue;float actual[4],expected[4];memcpy(actual,output+r->vertex*16,16);memcpy(expected,r->words,16);for(unsigned c=0;c<4;++c){float delta=actual[c]-expected[c];MUST(delta<.00002f&&delta>-.00002f);}matched++;}MUST(matched==4);}
}
int main(void){native_viewport_vertices();native_cases();isolation_cases();absent_material_cases();normal_cases();printf("PASS actual SDK ProcessVertices lighting: %u checks, original system RGB observations, real material/processing entry points and shared kernels, source alpha selection, zero-power profiles, offset/guard isolation, fog precedence, recording/failure paths, default D3D8/9 alpha\n",checks);return 0;}
