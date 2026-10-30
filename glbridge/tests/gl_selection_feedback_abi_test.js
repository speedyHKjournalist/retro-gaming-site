"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const { body, transport, run, GL } = require("./gl_cpu_abi_harness.js");
const source = fs.readFileSync(path.join(__dirname, "../openglproxy/opengl32_proxy.c"), "utf8");
const code = `
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl_protocol.h"
typedef unsigned int GLenum,GLuint;typedef int GLint,GLsizei,BOOL;typedef float GLfloat;
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define v86gl_trace(...) ((void)0)
static int v86gl_trace_call_allowed(void){return 0;}
${Object.entries(GL).filter(([, v]) => Number.isInteger(v) && v >= 0).map(([n, v]) => "#define GL_" + n + " 0x" + v.toString(16) + "u").join("\n")}
static uint8_t selected[4096],feedback[400016];
/* CPU metadata contains guest addresses. Map those addresses to host test
 * storage when the actual C reader copies each returned chunk. */
static void copy_guest(void* destination,const void* source,size_t size){
    uintptr_t at=(uintptr_t)destination;
    if(at>=0x1000000&&at<0x1000000+sizeof(selected)){assert(at-0x1000000+size<=sizeof(selected));memcpy(selected+at-0x1000000,source,size);}
    else if(at>=0x2000000&&at<0x2000000+sizeof(feedback)){assert(at-0x2000000+size<=sizeof(feedback));memcpy(feedback+at-0x2000000,source,size);}
    else memcpy(destination,source,size);
}
#define CopyMemory copy_guest
static unsigned int maximum,large_chunks;
#define GL_CPU_ABI_BEFORE_SEND do{if(wire_size>maximum)maximum=wire_size;if(*(uint16_t*)wire==GLFN_QUERY_RENDER_MODE){uint32_t h=*(uint16_t*)(wire+2)==0xFFFF?8:4;if(read_u32le(wire+h)==3&&read_u32le(wire+h+20)>1000)large_chunks++;}}while(0)
${transport}
static uint32_t g_dma_size;
static BOOL wait_for_host_status(const uint8_t* p,const char* name){(void)name;return read_u32le(p)==1;}
${body(source, "query_render_mode", "static BOOL ")}
${body(source, "emit_name_command", "static void ")}
${["glSelectBuffer", "glFeedbackBuffer", "glInitNames", "glLoadName", "glPushName", "glPopName", "glPassThrough", "glBegin", "glEnd", "glVertex3f", "glVertex4f"].map(name => body(source, name, "void APIENTRY ")).join("\n")}
${body(source, "glRenderMode", "GLint APIENTRY ")}
static uint32_t host_error(void){uint32_t request[4]={0};uint8_t* args;reserve_pci_record(GLFN_QUERY_ERROR,NULL,16,&args);memcpy(args,request,16);emit_pci_batch(0);assert(read_u32le(args)==1);return read_u32le(args+4);}
int main(void){
    memset(selected,0xA5,sizeof(selected));glSelectBuffer(16,(GLuint*)0x1000000);assert(glRenderMode(GL_SELECT)==0);
    glInitNames();glPushName(7);glBegin(GL_POINTS);glVertex3f(0,0,0);glEnd();assert(glRenderMode(GL_RENDER)==1);
    uint32_t* hit=(uint32_t*)selected;assert(hit[0]==1&&hit[1]==0x80000000u&&hit[2]==0x80000000u&&hit[3]==7&&hit[4]==0xA5A5A5A5);
    uint32_t viewport[]={2,3,10,20};emit_gl_call(GLFN_VIEWPORT,viewport,16);GLfloat color[]={0.25f,0.5f,0.75f,1},tex[]={2,3,4,5};
    emit_gl_call(GLFN_COLOR4F,color,16);emit_gl_call(GLFN_TEX_COORD4F,tex,16);
    GLfloat* values=(GLfloat*)feedback;for(unsigned int i=0;i<100004;i++)values[i]=-12345;
    glFeedbackBuffer(128,GL_4D_COLOR_TEXTURE,(GLfloat*)0x2000000);assert(glRenderMode(GL_FEEDBACK)==0);
    glBegin(GL_POINTS);glVertex4f(0.5,1,0,2);glEnd();assert(glRenderMode(GL_RENDER)==13);
    GLfloat expected[]={GL_POINT_TOKEN,8.25,18,0.5,2,0.25,0.5,0.75,1,2,3,4,5};assert(!memcmp(values,expected,sizeof(expected)));assert(values[13]==-12345);
    glFeedbackBuffer(100000,GL_2D,(GLfloat*)0x2000000);assert(glRenderMode(GL_FEEDBACK)==0);
    for(unsigned int i=0;i<40000;i++)glPassThrough((GLfloat)i);assert(glRenderMode(GL_RENDER)==80000);
    for(unsigned int i=0;i<40000;i++){assert(values[i*2]==GL_PASS_THROUGH_TOKEN);assert(values[i*2+1]==(GLfloat)i);}assert(values[80000]==-12345);
    assert(large_chunks==2&&maximum<=262196&&!error&&!host_error());
    glLoadName(1);glPopName();glInitNames();assert(!host_error());
    glBegin(GL_POINTS);glPushName(2);glEnd();assert(host_error()==GL_INVALID_OPERATION);return 0;
}
`;
run(code, (shadow, gpu) => {
    assert.equal(shadow.current.state.selection.mode, GL.RENDER); assert.equal(shadow.current.state.selection.pending, null);
    assert.equal(shadow.current.state.selection.selectPointer, 0x1000000); assert.equal(shadow.current.state.selection.feedbackPointer, 0x2000000);
    assert.equal(gpu.stats.draws, 0);
}).then(() => console.log("gl_selection_feedback_abi_test: actual C selection/feedback, names, coordinates, guest addresses and bounded 320 KiB result chunks passed in both modes"))
    .catch(error => { console.error(error); process.exitCode = 1; });
