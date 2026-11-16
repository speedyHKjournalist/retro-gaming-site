"use strict";
const assert = require("node:assert/strict"), fs = require("node:fs"), path = require("node:path");
const os = require("node:os"), { spawnSync } = require("node:child_process");
const root = path.resolve(__dirname, ".."), source = fs.readFileSync(path.join(root, "v86gl_driver/v86gl_driver.c"), "utf8");
const prefix = source.slice(source.indexOf("typedef struct V86GL_ARENA"), source.indexOf("static void NTAPI v86gl_unload"));
assert.ok(prefix.includes("v86gl_process_notify") && prefix.includes("v86gl_device_control"));
const directory = fs.mkdtempSync(path.join(os.tmpdir(), "v86gl-driver-owners-"));
try {
    const input = path.join(directory, "owners.c"), binary = path.join(directory, "owners");
    fs.writeFileSync(input, `
#include "v86gl_kernel.h"
${prefix}
PVOID v86gl_try_map_user(PVOID mdl,PVOID (*map)(PVOID)) { return map(mdl); }
static V86GL_DEVICE_EXTENSION extension;
static DEVICE_OBJECT device={&extension};
static void lifecycle(FILE_OBJECT *file,unsigned op) {
    IRP irp={0};irp.stack.FileObject=file;irp.stack.MajorFunction=op;
    assert(v86gl_create_close(&device,&irp)==STATUS_SUCCESS);
}
static NTSTATUS control(FILE_OBJECT *file,unsigned op,void *data,unsigned input,unsigned output) {
    IRP irp={0};irp.stack.FileObject=file;irp.AssociatedIrp.SystemBuffer=data;
    irp.stack.Parameters.DeviceIoControl.IoControlCode=op;
    irp.stack.Parameters.DeviceIoControl.InputBufferLength=input;
    irp.stack.Parameters.DeviceIoControl.OutputBufferLength=output;
    return v86gl_device_control(&device,&irp);
}
static NTSTATUS mapping(FILE_OBJECT *file,int d3d) {
    V86GLMapBuffer result={0};return control(file,d3d?V86GL_IOCTL_MAP_D3D_BUFFER:V86GL_IOCTL_MAP_BUFFER,&result,0,sizeof(result));
}
static V86GL_ARENA *arena(FILE_OBJECT *file) { return ((V86GL_FILE*)file->FsContext)->arena; }
static NTSTATUS submit(FILE_OBJECT *file) {
    V86GLSubmit data={44,0};return control(file,V86GL_IOCTL_SUBMIT,&data,sizeof(data),0);
}
static void descriptor(V86GL_ARENA *a) {
    V86GLDMADesc *d=a->buffer;d->magic=V86GL_MAGIC;d->version=V86GL_VERSION;d->command_count=1;d->command_bytes=12;
}
int main(void) {
    FILE_OBJECT gl1={0},gl2={0},a1={0},a2={0},b={0},foreign={0};
    g_extension=&extension;extension.transport.owner_arenas=TRUE;
    lifecycle(&gl1,IRP_MJ_CREATE);lifecycle(&gl2,IRP_MJ_CREATE);
    assert(mapping(&gl1,0)==0&&mapping(&gl2,0)==0);
    V86GL_ARENA *gl=arena(&gl1);assert(gl==arena(&gl2)&&gl->references==2&&!gl->owner);
    assert(!((unsigned char*)gl->buffer)[0]&&!((unsigned char*)gl->buffer)[gl->bytes-1]);
    lifecycle(&gl1,IRP_MJ_CLEANUP);assert(arena(&gl2)==gl&&gl->references==1&&!unmapped);
    lifecycle(&gl1,IRP_MJ_CLOSE);
    current=&processes[1];lifecycle(&foreign,IRP_MJ_CREATE);assert(mapping(&foreign,0)==STATUS_DEVICE_BUSY);
    lifecycle(&b,IRP_MJ_CREATE);assert(mapping(&b,1)==0);V86GL_ARENA *ba=arena(&b);
    assert(ba->owner&&ba->process==current&&ba->buffer!=gl->buffer);descriptor(ba);assert(submit(&b)==0);
    current=&processes[0];lifecycle(&a1,IRP_MJ_CREATE);lifecycle(&a2,IRP_MJ_CREATE);
    assert(mapping(&a1,1)==0&&mapping(&a2,1)==0);V86GL_ARENA *aa=arena(&a1);
    assert(aa==arena(&a2)&&aa->owner!=ba->owner&&aa->buffer!=gl->buffer&&aa->buffer!=ba->buffer);
    descriptor(aa);assert(submit(&a1)==0);lifecycle(&a1,IRP_MJ_CLEANUP);lifecycle(&a1,IRP_MJ_CLOSE);
    assert(!aa->revoked&&aa->references==1);
    current=&processes[1];assert(submit(&a2)==STATUS_ACCESS_DENIED);
    // A duplicated file's final cleanup may run in a foreign process. Its
    // original address space is used once; CLOSE in System does not unmap.
    lifecycle(&gl2,IRP_MJ_CLEANUP);assert(attached==1&&unmapped==1&&current==&processes[1]);
    current=&processes[2];lifecycle(&gl2,IRP_MJ_CLOSE);assert(unmapped==1);
    current=&processes[0];v86gl_process_notify(NULL,(HANDLE)1,FALSE);processes[0].alive=0;
    assert(aa->revoked&&!aa->user_mapping&&unmapped==2);
    current=&processes[1];lifecycle(&a2,IRP_MJ_CLEANUP);lifecycle(&a2,IRP_MJ_CLOSE);assert(unmapped==2&&attached==1);
    assert(submit(&b)==0);
    // Reset registration and submit again: it renews B, independently of GL.
    memset(registered,0,sizeof(registered));extension.transport.registered=FALSE;assert(submit(&b)==0);
    unsigned before=unmapped;assert(control(&b,V86GL_IOCTL_UNMAP_BUFFER,NULL,0,0)==0&&unmapped==before+1);
    assert(mapping(&b,1)==0);ba=arena(&b);descriptor(ba);assert(submit(&b)==0);
    v86gl_process_notify(NULL,(HANDLE)2,FALSE);processes[1].alive=0;assert(ba->revoked);
    current=&processes[2];lifecycle(&b,IRP_MJ_CLOSE);lifecycle(&foreign,IRP_MJ_CLOSE);
    assert(!extension.arenas&&!buffers&&!mdls&&!pools);
    assert(!processes[0].references&&!processes[1].references);
    // Each allocation failure unwinds mappings, references and allocations.
    for(unsigned i=0;i<4;i++) {
        FILE_OBJECT failed={0};lifecycle(&failed,IRP_MJ_CREATE);
        if(i==0)fail_pool=1;if(i==1)fail_buffer=1;if(i==2)fail_mdl=1;if(i==3)fail_map=1;
        assert(mapping(&failed,1)==STATUS_INSUFFICIENT_RESOURCES);
        assert(!arena(&failed)&&!extension.arenas&&!buffers&&!mdls&&!processes[2].references);
        lifecycle(&failed,IRP_MJ_CLOSE);assert(!pools);
    }
    FILE_OBJECT old={0};lifecycle(&old,IRP_MJ_CREATE);extension.transport.owner_arenas=FALSE;
    assert(mapping(&old,1)==STATUS_NOT_SUPPORTED&&mapping(&old,0)==0);
    assert(submit(&old)==STATUS_INVALID_PARAMETER);descriptor(arena(&old));assert(submit(&old)==0);
    lifecycle(&old,IRP_MJ_CLEANUP);assert(submit(&old)==STATUS_INVALID_HANDLE);lifecycle(&old,IRP_MJ_CLOSE);
    assert(!pools&&!buffers&&!mdls&&!extension.arenas&&!processes[2].references);
    assert(submitted==6&&releases==5);
    puts("PASS actual C driver: process arenas, shared file references, cleanup/close, foreign attach, exit-before-address-space-deletion, reset renewal and allocation failure unwind");
    return 0;
}
`);
    const built = spawnSync(process.env.HOST_CC || "cc", ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
        "-I", path.join(__dirname, "native"), input, "-o", binary], { encoding: "utf8" });
    assert.equal(built.status, 0, built.stderr.slice(0, 4000) || String(built.error));
    const run = spawnSync(binary, [], { encoding: "utf8" });
    assert.equal(run.status, 0, run.stderr.slice(0, 1500) || String(run.error));process.stdout.write(run.stdout);
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
