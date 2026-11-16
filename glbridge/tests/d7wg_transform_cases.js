"use strict";
exports.nativeTransformCases = data => {
 const rows=data.matrices.map(r=>'{'+r.state+'u,'+r.phase+'u,{'+r.words.map(n=>n+'u').join(',')+'}}').join(',');
 const views=data.views.map(r=>'{'+r.hr+'u,{'+r.words.map(n=>n+'u').join(',')+'}}').join(',');
 return `
static const struct MatrixObservation{unsigned state,phase;DWORD words[16];}matrix_observations[]={${rows}};
static int verify_matrix(D3D7Device*d,unsigned state,unsigned phase){D3DMATRIX m;CHECK(!d3d7_device_GetTransform(d,state,&m));for(unsigned i=0;i<84;++i)if(matrix_observations[i].state==state&&matrix_observations[i].phase==phase){CHECK(!memcmp(&m,matrix_observations[i].words,64));return 0;}CHECK(0);return 1;}
static int native_transform_cases(D3D7Device*d){
 const float av[16]={1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1},bv[16]={2,0,0,0,0,3,0,0,0,0,4,0,5,6,7,1};D3DMATRIX a,b;memcpy(&a,av,64);memcpy(&b,bv,64);
 const unsigned states[]={1,2,3,4,5,6,16,17,18,19,20,21,22,23};
 for(unsigned n=0;n<14;++n){unsigned state=states[n];DWORD block;
 CHECK(!d3d7_device_SetTransform(d,state,&a)&&!d3d7_device_MultiplyTransform(d,state,&b)&&!verify_matrix(d,state,0));
 CHECK(!d3d7_device_SetTransform(d,state,&a)&&!d3d7_device_BeginStateBlock(d)&&!d3d7_device_MultiplyTransform(d,state,&b)&&!verify_matrix(d,state,1));
 CHECK(!d3d7_device_EndStateBlock(d,&block)&&!verify_matrix(d,state,2)&&!d3d7_device_SetTransform(d,state,&a)&&!d3d7_device_ApplyStateBlock(d,block)&&!verify_matrix(d,state,3)&&!d3d7_device_DeleteStateBlock(d,block));
 CHECK(!d3d7_device_SetTransform(d,state,&a)&&!d3d7_device_BeginStateBlock(d)&&!d3d7_device_SetTransform(d,state,&b)&&!d3d7_device_MultiplyTransform(d,state,&b)&&!verify_matrix(d,state,4));
 CHECK(!d3d7_device_EndStateBlock(d,&block)&&!d3d7_device_ApplyStateBlock(d,block)&&!verify_matrix(d,state,5)&&!d3d7_device_DeleteStateBlock(d,block));
 }
 const D3DVIEWPORT7 inputs[]={{0,0,128,128,0,1},{0,0,128,128,0,0},{0,0,128,128,1,0},{10,5,100,110,-2,4},{0,0,128,128,-1,-2},{0,0,128,128,2,3},{0,0,129,128,0,1},{1,0,128,128,0,1},{0,0,0,128,0,1},{0,0,128,0,0,1},{0xffffffffu,0,2,2,0,1}};
 const struct ViewObservation{DWORD hr,words[6];}views[]={${views}};
 for(unsigned i=0;i<11;++i){D3DVIEWPORT7 vp=inputs[i],out;CHECK((DWORD)d3d7_device_SetViewport(d,&vp)==views[i].hr);CHECK(!d3d7_device_GetViewport(d,&out)&&!memcmp(&out,views[i].words,24));}
 DWORD block;CHECK(!d3d7_device_SetTransform(d,1,&a)&&!d3d7_device_BeginStateBlock(d)&&!d3d7_device_SetTransform(d,1,&b));D3D7StateBlock*recording=d->recording_state_block;fail_opcode=D9WG_OP_SET_TRANSFORM;CHECK(d3d7_device_MultiplyTransform(d,1,&b)==D3DERR_INITFAILED&&d->recording_state_block==recording&&!memcmp(&d->transforms[1],&a,64)&&!memcmp(&recording->transforms[1],&b,64));CHECK(!d3d7_device_EndStateBlock(d,&block)&&!d3d7_device_DeleteStateBlock(d,block));
 printf("PASS actual transforms: 84 native ordinary/recorded matrix observations, 11 viewport queries, rejected bounds preservation and failed Multiply restores recording\\n");return 0;
}
`;
};
