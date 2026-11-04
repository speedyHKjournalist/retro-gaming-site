"use strict";
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process");
const root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-buffer-lock-mutants-")),policy=fs.readFileSync(root+"/runtime/d9wg_buffer_lock.c","utf8");
const cases=[
 ["read-only access lost","return D9WG_BUFFER_LOCK_READONLY;","return 0;"],
 ["write-only hint conflict not normalized","&& !(usage & D9WG_BUFFER_USAGE_WRITEONLY) && !streaming)","&& !streaming)"],
 ["streaming read-only conflict not normalized","&& !(usage & D9WG_BUFFER_USAGE_WRITEONLY) && !streaming)","&& !(usage & D9WG_BUFFER_USAGE_WRITEONLY))"],
 ["static streaming hints retained","if (!(usage & D9WG_BUFFER_USAGE_DYNAMIC) || pool > 1u)","if (0 && (!(usage & D9WG_BUFFER_USAGE_DYNAMIC) || pool > 1u))"],
 ["CPU-only streaming retained","if (!(usage & D9WG_BUFFER_USAGE_DYNAMIC) || pool > 1u)","if (!(usage & D9WG_BUFFER_USAGE_DYNAMIC) || 0u * pool > 1u)"],
 ["conflicting hints discard old data","return D9WG_BUFFER_LOCK_NOOVERWRITE;","return (streaming & D9WG_BUFFER_LOCK_DISCARD) ? D9WG_BUFFER_LOCK_DISCARD : D9WG_BUFFER_LOCK_NOOVERWRITE;"],
 ["valid discard lost","return streaming & D9WG_BUFFER_LOCK_DISCARD;","return 0;"],
 ["unknown hints not ignored","return streaming & D9WG_BUFFER_LOCK_DISCARD;","return requested;"],
 ["managed GPU hint lost","|| pool > 1u)","|| pool > 0u)"],
];
function method(source,name){const m=source.match(new RegExp("static HRESULT WINAPI "+name+"\\([^;]*?\\)\\n\\{[\\s\\S]*?\\n}"));assert(m,name);return m[0];}
function run(name,source,env,guard=false){const file=dir+"/mutated.c";fs.writeFileSync(file,source);const result=spawnSync(process.execPath,[path.join(__dirname,"d9wg_buffer_lock_test.js")],{env:{...process.env,...env(file)},encoding:"utf8"});assert(result.status!==0&&!result.signal,"survived or abnormal harness "+name);assert(!/error:|undefined reference|Undefined symbols|error generated|errors generated/.test(result.stderr+result.stdout),"Compiler failure is not a mutation kill: "+name+"\n"+result.stderr);assert.match(result.stderr,guard?/buffer lock guard/:/buffer lock line/,"native runtime failure required: "+name);if(guard)assert.match(result.stderr,/native signal=SIG(?:SEGV|BUS)/,"Expected controlled protected-page violation: "+name);}

try{
 for(const [name,before,after]of cases){assert.equal(policy.split(before).length,2,name);run(name,policy.replace(before,after),file=>({D9WG_BUFFER_LOCK_POLICY_SOURCE:file}));}
 for(const api of [8,9]){const original=fs.readFileSync(root+"/d3d"+api+"proxy/d3d"+api+"_proxy.c","utf8"),environment=file=>({D9WG_BUFFER_LOCK_API:String(api),D9WG_BUFFER_FRONTEND_SOURCE:file});for(const kind of ["vb","ib"]){const locked=method(original,kind+"_lock"),unlocked=method(original,kind+"_unlock");
  const sanitize="flags = d9wg_buffer_lock_flags(buffer->usage, buffer->pool, flags);";assert(locked.includes(sanitize));run("API "+api+" "+kind+" bypasses policy",original.replace(locked,locked.replace(sanitize,"flags &= UINT32_MAX;")),environment);
  run("API "+api+" "+kind+" clears full shadow on discard",original.replace(locked,locked.replace("    buffer->lock_flags = flags;","    buffer->lock_flags = flags;\n    if (flags & D3DLOCK_DISCARD) ZeroMemory(buffer->shadow, buffer->length);")),environment,true);
  const readonly="(buffer->lock_flags & D3DLOCK_READONLY)";assert(unlocked.includes(readonly));run("API "+api+" "+kind+" drops writable upload",original.replace(unlocked,unlocked.replace(readonly,"(1 || (buffer->lock_flags & D3DLOCK_READONLY))")),environment);
  run("API "+api+" "+kind+" failure retains lock",original.replace(unlocked,unlocked.replace("buffer->locked = FALSE;","buffer->locked = TRUE;")),environment);
 }
 const vb=method(original,"vb_lock");run("API "+api+" discard retains processed clip records",original.replace(vb,vb.replace("if (flags & D3DLOCK_DISCARD)","if (0 && (flags & D3DLOCK_DISCARD))")),environment);
 }
}finally{fs.rmSync(dir,{recursive:true,force:true});}
console.log("PASS buffer Lock mutations: 27/27 (23 runtime assertions,4 controlled protected-page faults),strict compilation;retail access/streaming policy, four frontend adapters, exact uploads, clip invalidation, unlock failure cleanup and protected DISCARD shadow pages");
