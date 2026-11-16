/* Shared SDK8/9 test inputs, deliberately independent of runtime headers. */
static const UINT sc_dropped[]={10,30,40,153,164,172,173};
static const DWORD sc_bias[]={0,1,8,16,17,0xffffffffu};
static const UINT sc_tss[]={13,14,25,15,16,17,18,19,20,21};
static const UINT sc_sampler[]={1,2,3,4,5,6,7,8,9,10};
static const DWORD sc_values[]={3,3,3,0x11223344u,2,2,1,0x3f000000u,2,4};
void sc_check(BOOL,const char*);void sc_hr(HRESULT,const char*);void sc_report(const char*);
