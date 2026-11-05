"use strict";
// Historical Device8 comparison; production facade coverage lives in d8wg_*_adapter/device tests and SDK XP probes.
const {readComparisonSource} = require("./legacy8_comparison.js");
const assert=require("node:assert/strict"),fs=require("node:fs"),path=require("node:path"),os=require("node:os"),{spawnSync}=require("node:child_process"),root=path.resolve(__dirname,".."),dir=fs.mkdtempSync(path.join(os.tmpdir(),"d9wg-private-mut-"));

function run(name,test,env){const r=spawnSync(process.execPath,[__dirname+'/'+test],{encoding:'utf8',timeout:30000,env:{...process.env,...env}});assert(!r.error,name+': '+r.error);assert.notEqual(r.status,0,name+' survived');const output=r.stdout+r.stderr;assert(!/error:|undefined reference|Undefined symbols/.test(output),name+' compile failure: '+output);assert(/Present parameters|Parameter preflight|Assertion|signal=/.test(output),name+' no runtime failure');}
function span(s,name){const m=new RegExp('static [^;{}\\n]*?\\b'+name+'\\([^;]*?\\)\\s*\\{').exec(s);assert(m,name);let end=s.indexOf('{',m.index)+1,depth=1;for(;depth;end++){if(s[end]==='{')depth++;if(s[end]==='}')depth--;}return[m.index,end];}
try{let killed=0;const runtime=readComparisonSource(root+'/runtime/d9wg_present_pacing.c','utf8'),start=runtime.indexOf('int d9wg_present_interval_valid('),end=runtime.indexOf('\nvoid ',start),part=runtime.slice(start,end);assert(start>=0&&end>start);for(const[name,old,replacement]of[
 ['unknown API accepted','if (api != 8 && api != 9)','if (0 && api != 8 && api != 9)'],
 ['windowed rules applied to wrong version','api == 8 && windowed','api == 9 && windowed'],
 ['default interval refused','if (!requested)\n        return 1;','if (!requested)\n        return 0;'],
 ['combined and reserved bits accepted','if (requested != 1 &&','if (0 && requested != 1 &&'],
 ['windowed D9 accepts multi-refresh','if (windowed && requested != 1','if (0 && windowed && requested != 1'],
 ['caps ignored','return (supported & requested) == requested;','return 1; (void)supported;'],
]){assert(part.includes(old),name);const file=dir+'/runtime.c';fs.writeFileSync(file,runtime.slice(0,start)+part.replace(old,replacement)+runtime.slice(end));run(name,'d9wg_present_parameters_test.js',{D9WG_PARAMETERS_RUNTIME:file});killed++;}
for(const api of[8,9]){const source=readComparisonSource(root+'/d3d'+api+'proxy/d3d'+api+'_proxy.c','utf8');for(const fn of['d3d_create_device','device_reset',api===8?'device_create_swapchain':'device_create_additional_swap_chain'])for(const[description,old,replacement]of[
 ['guard omitted','if (!d9wg_present_interval_valid(','if (0 && !d9wg_present_interval_valid('],
 ['wrong version policy','d9wg_present_interval_valid('+(api===9?'api':api)+',','d9wg_present_interval_valid('+(api===8?9:8)+','],
]){const[a,b]=span(source,fn),body=source.slice(a,b);assert(body.includes(old),fn);const file=dir+'/frontend.c';fs.writeFileSync(file,source.slice(0,a)+body.replace(old,replacement)+source.slice(b));run('API '+api+' '+fn+' '+description,'d9wg_present_parameters_entry_test.js',{['D'+api+'WG_PARAMETERS_FRONTEND']:file});killed++;}}
console.log('PASS presentation-parameter mutations: '+killed+'/18; version/window/caps/default/single-flag policy and all six actual preflight guards');}finally{fs.rmSync(dir,{recursive:true,force:true});}
