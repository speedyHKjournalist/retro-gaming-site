"use strict";
const assert = require("node:assert/strict");
exports.nativeScopeCases = function (reference) {
    const scope = [], aliases = [];
    for (const line of reference.cases) {
        const values = Object.fromEntries([...line.matchAll(/(\w+)=([^ ]+)/g)].map(m => [m[1], m[2]]));
        const integer = name => Number(values[name]), hex = name => parseInt(values[name], 16), words = name => values[name].split(",").map(v => parseInt(v, 16));
        if (line.startsWith("SCOPE ")) scope.push([integer("kind"), integer("state"), integer("type"), ...["a", "b", "first", "second", "applied", "ended"].map(hex), ...words("endhr"), ...words("hr")]);
        else if (line.startsWith("ALIAS ")) aliases.push([integer("test"), ["during", "end", "apply"].indexOf(values.phase), integer("kind"), integer("state"), hex("value"), hex("hr")]);
        else assert.fail("Unknown native observation: " + line);
    }
    assert.equal(scope.length, 548);
    assert.equal(aliases.length, 504);
    const array = rows => rows.map(row => "{" + row.map(v => v + "u").join(",") + "}").join(",");
    return `
static const uint32_t scope_reference[548][18]={${array(scope)}};
static const uint32_t alias_reference[504][6]={${array(aliases)}};
static HRESULT scope_set(D3D7Device *d,unsigned kind,unsigned state,DWORD value){return kind?d3d7_device_SetTextureStageState(d,0,state,value):d3d7_device_SetRenderState(d,state,value);}
static HRESULT scope_get(D3D7Device *d,unsigned kind,unsigned state,DWORD *value){return kind?d3d7_device_GetTextureStageState(d,0,state,value):d3d7_device_GetRenderState(d,state,value);}
static int query_aliases(D3D7Device *d,unsigned test,unsigned phase){
 for(unsigned i=0;i<504;++i){const uint32_t *r=alias_reference[i];if(r[0]!=test||r[1]!=phase)continue;DWORD value=0xcccccccc;HRESULT hr=scope_get(d,r[2],r[3],&value);if((uint32_t)hr!=r[5]||value!=r[4]){fprintf(stderr,"D3D7 state scope alias row %u: got %08x hr %08x expected %08x hr %08x\\n",i,value,(uint32_t)hr,r[4],r[5]);return 1;}}return 0;
}
static int native_scope_cases(D3D7Device *d){
 for(unsigned i=0;i<548;++i){const uint32_t *r=scope_reference[i];DWORD first=0xcccccccc,second=0xcccccccc,applied=0xcccccccc,ended=0xcccccccc,block=0;uint32_t hr[9];
 hr[2]=(uint32_t)scope_set(d,r[0],r[1],r[3]);hr[3]=(uint32_t)scope_get(d,r[0],r[1],&first);
 hr[4]=(uint32_t)(r[2]?d3d7_device_CreateStateBlock(d,r[2],&block):d3d7_device_BeginStateBlock(d));
 hr[5]=(uint32_t)scope_set(d,r[0],r[1],r[4]);hr[6]=(uint32_t)scope_get(d,r[0],r[1],&second);
 hr[0]=r[2]?0:(uint32_t)d3d7_device_EndStateBlock(d,&block);hr[1]=(uint32_t)scope_get(d,r[0],r[1],&ended);
 hr[7]=(uint32_t)d3d7_device_ApplyStateBlock(d,block);hr[8]=(uint32_t)scope_get(d,r[0],r[1],&applied);CHECK(d3d7_device_DeleteStateBlock(d,block)==0);
 if(first!=r[5]||second!=r[6]||applied!=r[7]||ended!=r[8]||memcmp(hr,r+9,sizeof(hr))){fprintf(stderr,"D3D7 state scope row %u kind %u state %u type %u: first %08x second %08x applied %08x ended %08x\\n",i,r[0],r[1],r[2],first,second,applied,ended);return 1;}}
 for(unsigned test=0;test<12;++test){
 scope_set(d,0,3,1);scope_set(d,0,17,1);scope_set(d,0,18,1);scope_set(d,0,21,2);CHECK(scope_set(d,0,128,0)==0);CHECK(scope_set(d,1,16,1)==0&&scope_set(d,1,17,1)==0&&scope_set(d,1,18,1)==0);
 if(test>=6)CHECK(d3d7_device_BeginStateBlock(d)==0);
 switch(test%6){
 case 0:scope_set(d,0,3,3);scope_set(d,1,13,2);break;
 case 1:scope_set(d,1,13,2);scope_set(d,0,3,3);break;
 case 2:scope_set(d,0,17,2);scope_set(d,1,16,1);scope_set(d,0,18,6);scope_set(d,1,17,1);break;
 case 3:scope_set(d,1,16,2);scope_set(d,0,17,1);scope_set(d,1,18,3);scope_set(d,0,18,1);break;
 case 4:scope_set(d,0,21,2);scope_set(d,1,1,7);break;
 case 5:scope_set(d,1,1,7);scope_set(d,0,21,2);break;
 }
 CHECK(!query_aliases(d,test,0));if(test>=6){DWORD block;CHECK(d3d7_device_EndStateBlock(d,&block)==0&&!query_aliases(d,test,1));CHECK(d3d7_device_ApplyStateBlock(d,block)==0&&!query_aliases(d,test,2)&&d3d7_device_DeleteStateBlock(d,block)==0);}
 }
 return 0;
}
`;
};
exports.legacyStateCases = function (source) {
    function extract(name) {
        const match = new RegExp("static HRESULT WINAPI " + name + "\\([^;]*?\\)\\s*\\{").exec(source);
        assert(match, name);
        let at = source.indexOf("{", match.index) + 1, depth = 1;
        for (; depth; ++at) {
            assert(at < source.length);
            if (source[at] === "{") ++depth;
            if (source[at] === "}") --depth;
        }
        return source.slice(match.index, at);
    }
    return `
typedef IDirect3DDevice7 IDirect3DDevice3;
typedef DWORD D3DTEXTUREHANDLE;
#define SUCCEEDED(x) ((x)>=0)
static D3D7Device *d3d3_device_from_iface(IDirect3DDevice3 *d){return d;}
static DDSurface *d3d_legacy_find_texture(Owner *owner,DWORD handle){(void)owner;(void)handle;return NULL;}
${["d3d3_device_GetRenderState", "d3d3_device_SetRenderState"].map(extract).join("\n")}
static int legacy_state_cases(D3D7Device *d){
 for(unsigned state=2;state<=152;++state){if(!d3d_legacy_render_state_valid(state)||d3d7_render_state_valid(state))continue;DWORD value=0xcccccccc;CHECK(d3d7_device_SetRenderState(d,state,0x12345678)==DDERR_INVALIDPARAMS&&d3d7_device_GetRenderState(d,state,&value)==DDERR_INVALIDPARAMS&&value==0xcccccccc);CHECK(d3d3_device_SetRenderState(d,state,state==21?2:1)==0);CHECK(d3d3_device_GetRenderState(d,state,&value)==0&&value==(state==21?2:1));value=0xcccccccc;CHECK(d3d7_device_GetRenderState(d,state,&value)==DDERR_INVALIDPARAMS&&value==0xcccccccc);}
 unsigned sent=emissions;CHECK(d3d7_device_SetRenderState(d,3,0x2468)==DDERR_INVALIDPARAMS&&d3d7_device_SetRenderState(d,3,0x2468)==0&&emissions==sent);CHECK(d3d7_device_BeginStateBlock(d)==0&&d3d7_device_SetRenderState(d,3,0x2468)==DDERR_INVALIDPARAMS);DWORD empty;CHECK(d3d7_device_EndStateBlock(d,&empty)==0&&d3d7_device_ApplyStateBlock(d,empty)==0&&d3d7_device_DeleteStateBlock(d,empty)==0&&emissions==sent);
 CHECK(d3d3_device_SetRenderState(d,1,0)==0);DWORD value=0xcccccccc;CHECK(d3d3_device_GetRenderState(d,1,&value)==0&&value==0);return 0;
}
`;
};
