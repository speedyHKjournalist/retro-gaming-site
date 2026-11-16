static DDSurface make(DDrawObject*o,DDSurfaceMemory*m,UINT format,UINT w,UINT h,UINT caps)
{
    UINT bytes=format_bytes_per_pixel(format),block=format_compressed_block_bytes(format);
    UINT pitch=(block?(w+3)/4*block:w*bytes)+8,rows=block?(h+3)/4:h;
    memset(m,0,sizeof(*m));memset(m->bits,0xa5,1024);memset(m->gpu,0x3b,1024);
    m->bytes=rows*pitch;m->content_version=7;MUST(m->bytes<=1024);
    return(DDSurface){.owner=o,.memory=m,.shadow=m->bits,.width=w,.height=h,.pitch=pitch,.bytes_per_pixel=bytes,.format=format,.handle=77,.desc={.ddsCaps={.dwCaps=caps}}};
}
static void packed_blue(DDSurface*s)
{
    for(UINT y=0;y<s->height;y++)for(UINT x=0;x<s->width;x++){const BYTE blue[2]={31,128};memcpy(s->shadow+y*s->pitch+x*2,blue,2);}
}
static void solid(DDSurface*s,WORD color,BYTE alpha)
{
    UINT block=format_compressed_block_bytes(s->format),kind=d9wg_bc_kind(s->format);
    for(UINT y=0;y<(s->height+3)/4;y++)for(UINT x=0;x<(s->width+3)/4;x++){
        BYTE*p=s->shadow+y*s->pitch+x*block;memset(p,0,block);
        if(kind==2||kind==3)for(UINT a=0;a<8;a++)p[a]=(alpha>>4)*17u;
        if(kind>=4)p[0]=p[1]=alpha;
        p+=kind==1?0:8;p[0]=p[2]=(BYTE)color;p[1]=p[3]=(BYTE)(color>>8);
    }
}
static void native_encodings(void)
{
    for(UINT n=0;n<sizeof(native)/sizeof(native[0]);n++)for(UINT pool=0;pool<3;pool++){
        DDrawObject o={0};DDSurfaceMemory sm,dm;
        DDSurface s=make(&o,&sm,21,4,4,pool==2?0:DDSCAPS_SYSTEMMEMORY),d=make(&o,&dm,native[n].format,4,4,pool==1?0:DDSCAPS_SYSTEMMEMORY);
        s.next=&d;o.surfaces=&s;for(UINT y=0;y<4;y++)memcpy(s.shadow+y*s.pitch,native[n].pixels+y*4,16);
        upload_target=&d;unsigned u=uploads;MUST(emit_blt(&d,NULL,&s,NULL,0,0,0));
        UINT block=format_compressed_block_bytes(d.format);MUST(!memcmp(d.shadow,native[n].bytes,block)&&!memcmp(dm.gpu,native[n].bytes,block));
        for(UINT b=block;b<1024;b++)MUST(dm.bits[b]==0xa5);
        MUST(uploads==u+1&&!dm.gpu_dirty&&dm.content_version==8&&!d.upload_pending);
        BOOL opaque=TRUE;for(UINT i=0;i<16;i++)if((native[n].pixels[i]>>24)!=255)opaque=FALSE;
        if(native[n].format==DDWG_FMT_DXT1&&opaque){DDSurfaceMemory tm;DDSurface t=make(&o,&tm,DDWG_FMT_DXT3,4,4,0);d.next=&t;upload_target=&t;MUST(emit_blt(&t,NULL,&d,NULL,0,0,0));for(UINT i=0;i<8;i++)MUST(t.shadow[i]==255);MUST(!memcmp(t.shadow+8,native[n].bytes,8));}
    }
}
static void stretches(void)
{
    const UINT formats[]={25,DDWG_FMT_DXT1,DDWG_FMT_DXT3};
    const UINT sizes[5][4]={{4,8,0,0},{8,4,0,0},{4,2,0,0},{4,6,0,0},{4,8,2,2}};
    for(UINT pool=0;pool<3;pool++)for(UINT shape=0;shape<5;shape++)for(UINT sf=0;sf<3;sf++)for(UINT df=1;df<3;df++){
        DDrawObject o={0};DDSurfaceMemory rm,sm,dm;UINT sw=sizes[shape][0],end=sizes[shape][1],width=(end+3)&~3u;
        DDSurface rb=make(&o,&rm,25,8,8,DDSCAPS_SYSTEMMEMORY),s=make(&o,&sm,formats[sf],sw,sw,pool==2?0:DDSCAPS_SYSTEMMEMORY),d=make(&o,&dm,formats[df],width,width,pool==1?0:DDSCAPS_SYSTEMMEMORY);
        o.surfaces=&rb;rb.next=&s;s.next=&d;packed_blue(&rb);RECT sr={(LONG)sizes[shape][2],(LONG)sizes[shape][2],(LONG)sw,(LONG)sw},dr={(LONG)sizes[shape][3],(LONG)sizes[shape][3],(LONG)end,(LONG)end};
        if(sf){memset(sm.bits,0,sm.bytes);upload_target=&s;MUST(emit_blt(&s,&sr,&rb,&sr,0,0,0));}else packed_blue(&s);
        solid(&d,0xf800,255);BYTE before[1024];memcpy(before,dm.bits,1024);upload_target=&d;MUST(emit_blt(&d,&dr,&s,&sr,0,0,0));
        for(UINT y=0;y<width;y++)for(UINT x=0;x<width;x++){BYTE rgba[16][4];UINT block=format_compressed_block_bytes(d.format);MUST(d9wg_bc_decode(d.format,d.shadow+y/4*d.pitch+x/4*block,rgba));BYTE*p=rgba[(y%4)*4+x%4];BOOL inside=x>=(UINT)dr.left&&x<(UINT)dr.right&&y>=(UINT)dr.top&&y<(UINT)dr.bottom;MUST(p[0]==(inside?0:255)&&!p[1]&&p[2]==(inside?255:0)&&p[3]==255);}
        UINT row_bytes=(width+3)/4*format_compressed_block_bytes(d.format);for(UINT b=0;b<1024;b++)if(b>=dm.bytes||b%d.pitch>=row_bytes)MUST(dm.bits[b]==before[b]);
    }
}
static void small_mips(void)
{
    for(UINT width=1;width<=3;width++)for(UINT kind=1;kind<=5;kind++){
        DDrawObject o={0};DDSurfaceMemory sm,dm;DDSurface s=make(&o,&sm,25,width,width,DDSCAPS_SYSTEMMEMORY),d=make(&o,&dm,0x30545844u+(kind<<24),width,width,0);
        s.next=&d;o.surfaces=&s;packed_blue(&s);solid(&d,0xf800,255);upload_target=&d;MUST(emit_blt(&d,NULL,&s,NULL,0,0,0));BYTE rgba[16][4];MUST(d9wg_bc_decode(d.format,d.shadow,rgba));for(UINT i=0;i<16;i++)MUST(!rgba[i][0]&&!rgba[i][1]&&rgba[i][2]==255&&rgba[i][3]==255);
    }
}
static void aliases(void)
{
    for(UINT kind=1;kind<=5;kind++)for(UINT reverse=0;reverse<2;reverse++){
        DDrawObject o={0};DDSurfaceMemory m;DDSurface s=make(&o,&m,0x30545844u+(kind<<24),12,12,DDSCAPS_SYSTEMMEMORY),d=s;d.next=&s;o.surfaces=&d;
        for(UINT i=0;i<1024;i++)m.bits[i]=(BYTE)(i*37+reverse);BYTE before[1024],expected[1024];memcpy(before,m.bits,1024);memcpy(expected,before,1024);
        RECT sr=reverse?(RECT){4,4,12,12}:(RECT){0,0,8,8},dr=reverse?(RECT){0,0,8,8}:(RECT){4,4,12,12};UINT block=format_compressed_block_bytes(s.format);
        for(UINT y=0;y<2;y++)memcpy(expected+(dr.top/4+y)*d.pitch+dr.left/4*block,before+(sr.top/4+y)*s.pitch+sr.left/4*block,2*block);
        upload_target=&d;unsigned a=allocations;MUST(emit_blt(&d,&dr,&s,&sr,0,0,0));MUST(!memcmp(m.bits,expected,1024)&&allocations==a+1&&!d.upload_pending&&!s.upload_pending);
    }
    DDrawObject o={0};DDSurfaceMemory m;DDSurface s=make(&o,&m,DDWG_FMT_DXT3,8,8,DDSCAPS_SYSTEMMEMORY),d=s;d.next=&s;o.surfaces=&d;solid(&s,0xf800,255);BYTE blue[16]={255,255,255,255,255,255,255,255,31,0,31,0};memcpy(s.shadow,blue,16);RECT sr={0,0,4,4},dr={2,2,8,8};upload_target=&d;MUST(emit_blt(&d,&dr,&s,&sr,0,0,0));for(UINT y=0;y<8;y++)for(UINT x=0;x<8;x++){BYTE rgba[16][4];d9wg_bc_decode(d.format,d.shadow+y/4*d.pitch+x/4*16,rgba);BYTE*p=rgba[(y%4)*4+x%4];BOOL is_blue=(x<4&&y<4)||(x>=2&&y>=2);MUST(p[0]==(is_blue?0:255)&&!p[1]&&p[2]==(is_blue?255:0)&&p[3]==255);}
}
static void premultiplication(void)
{
    for(UINT kind=2;kind<=4;kind+=2){DDrawObject o={0};DDSurfaceMemory sm,dm;DDSurface s=make(&o,&sm,21,4,4,DDSCAPS_SYSTEMMEMORY),d=make(&o,&dm,0x30545844u+(kind<<24),4,4,0);s.next=&d;o.surfaces=&s;solid(&d,0x4000,128);for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++){DWORD blue=0x800000ff;memcpy(s.shadow+y*s.pitch+x*4,&blue,4);}RECT part={0,0,2,4};upload_target=&d;MUST(emit_blt(&d,&part,&s,&part,0,0,0));BYTE rgba[16][4];d9wg_bc_decode(d.format,d.shadow,rgba);for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++){BYTE*p=rgba[y*4+x];MUST(p[0]==(x<2?0:66)&&!p[1]&&p[2]==(x<2?132:0)&&p[3]==(kind==2?136:128));}
    }
    DDSurfaceMemory sm,dm;DDrawObject o={0};DDSurface s=make(&o,&sm,DDWG_FMT_DXT2,4,4,0),d=make(&o,&dm,DDWG_FMT_DXT4,4,4,0);s.next=&d;o.surfaces=&s;solid(&s,0x4000,128);solid(&d,0x0010,128);RECT part={0,0,2,4};upload_target=&d;MUST(emit_blt(&d,&part,&s,&part,0,0,0));BYTE rgba[16][4];d9wg_bc_decode(d.format,d.shadow,rgba);for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++){BYTE*p=rgba[y*4+x];MUST(p[0]==(x<2?66:0)&&p[2]==(x<2?0:132));}
}
static void failures(void)
{
    DDrawObject o={0};DDSurfaceMemory sm,dm;DDSurface s=make(&o,&sm,25,8,8,DDSCAPS_SYSTEMMEMORY),d=make(&o,&dm,DDWG_FMT_DXT3,8,8,0),alias=d;s.next=&d;d.next=&alias;o.surfaces=&s;packed_blue(&s);solid(&d,0xf800,255);BYTE before[1024];memcpy(before,dm.bits,1024);upload_target=&d;RECT part={1,1,7,7},bad={0,0,9,8};unsigned u=uploads;MUST(!surface_bc_blt(&d,&bad,&s,NULL)&&!memcmp(before,dm.bits,1024)&&uploads==u);MUST(!surface_bc_blt(&d,NULL,&s,&bad));SIZE_T bytes=dm.bytes;dm.bytes=d.pitch+2*format_compressed_block_bytes(d.format)-1;MUST(!surface_bc_blt(&d,NULL,&s,NULL));dm.bytes=bytes;UINT pitch=d.pitch;d.pitch=31;MUST(!surface_bc_blt(&d,NULL,&s,NULL));d.pitch=pitch;
    fail_allocation=allocations+1;MUST(!surface_bc_blt(&d,&part,&s,&part)&&!memcmp(before,dm.bits,1024)&&dm.content_version==7);fail_allocation=0;
    DDSurface saved=s;s=d;s.next=&d;o.surfaces=&s;for(UINT which=1;which<=2;which++){fail_allocation=allocations+which;MUST(!surface_bc_blt(&d,&part,&s,&part)&&!memcmp(before,dm.bits,1024)&&dm.content_version==7);}fail_allocation=0;s=saved;s.next=&d;o.surfaces=&s;
    sm.gpu_dirty=TRUE;fail_read=&sm;MUST(!surface_bc_blt(&d,&part,&s,&part)&&!memcmp(before,dm.bits,1024));sm.gpu_dirty=FALSE;dm.gpu_dirty=TRUE;fail_read=&dm;MUST(!surface_bc_blt(&d,&part,&s,&part)&&!memcmp(before,dm.bits,1024));dm.gpu_dirty=FALSE;fail_read=NULL;
    fail_upload=TRUE;MUST(!surface_bc_blt(&d,&part,&s,&part)&&d.upload_pending&&alias.upload_pending&&!dm.gpu_dirty&&dm.content_version==8);fail_upload=FALSE;upload_target=&alias;MUST(prepare_surface_for_host(&alias)&&!d.upload_pending&&!alias.upload_pending&&dm.content_version==8);
    for(UINT flags=1;flags<256;flags++)MUST(!surface_can_bc_blt(&d,&s,flags));d.clipper=(void*)1;MUST(!surface_can_bc_blt(&d,&s,0));d.clipper=NULL;UINT fmt=s.format;s.format=DDWG_FMT_P8;s.bytes_per_pixel=1;MUST(!surface_can_bc_blt(&d,&s,0));s.format=fmt;s.bytes_per_pixel=1;MUST(!surface_can_bc_blt(&d,&s,0));s.bytes_per_pixel=2;d.format=DDWG_FMT_A8R8G8B8;MUST(!surface_can_bc_blt(&d,&s,0));d.format=DDWG_FMT_DXT3;s.format=DDWG_FMT_DXT2;MUST(!surface_bc_blt(&d,NULL,&s,NULL));MUST(!surface_can_bc_blt(&d,NULL,0));
}
int main(void){native_encodings();stretches();small_mips();aliases();premultiplication();failures();MUST(!emits);printf("PASS actual compressed DDraw blit: %u checks, 42 native encodings, 90 stretch/conversion pool cases, small-mip padding, raw/scaled aliases, stored premultiplication, guards and failed upload retry\n",checks);return 0;}
