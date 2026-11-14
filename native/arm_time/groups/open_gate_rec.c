// SPDX-License-Identifier: AGPL-3.0-only
// SPDX-FileCopyrightText: 2026 RdWing
/**
 * z63patch - Nikon Z6III Modifications
 * AGPLv3 Open Source. Use is subject to license terms.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * @package z63patch
 * @license AGPLv3 License (https://opensource.org/licenses/AGPL-3.0)
 *
 *   Copyright (C) 2026 RdWing
 *
 */
/* G10 part 10b: the recording side of open gate. Included by open_gate.c (same
 * translation unit, after 10a's helpers). Declared by z63patch/gate_groups/open_gate_rec.py.
 *
 * Every adapter here is a call-site post-fix (BL->BL) or a descriptor-word wrapper (Data): it
 * takes z63g_page() first and the phase, calls the stock function exactly once with the caller's
 * x0-x7, and only in LIVE/DRAINING edits what that call just wrote. In PASS it is the stock call.
 *
 * The decision (og_rec_kind): the kind the recording was planned with, i.e. the plan latch
 * (og_plan_kind; re-plan build: no longer confirmed by the choice og_size, which could
 * differ from the plan after a menu write in movie view). It does not change during a recording:
 * g10_replan refuses while movie work or outstanding is nonzero, and the plan post-fix only lowers
 * it when stock itself chose another record. So a recording
 * that is in progress when the gate drains keeps one geometry to its end; an integrity break
 * passes through at once.
 * Each edit also requires the output to hold the stock value of that kind's path (received
 * geometry): a writer never turns a value it does not know into an open-gate one.
 *
 * Diagnostics in G10 state +0x20..+0x24 (10b's bytes): the kind the last call of each writer
 * applied (0: left stock), written only on a LIVE/DRAINING call on the writer's open-gate path. */

#define OG_REC_NRAW   (OG_REC_BASE+0u)
#define OG_REC_HEIGHT (OG_REC_BASE+1u)
#define OG_REC_PROXY  (OG_REC_BASE+2u)
#define OG_REC_CROP   (OG_REC_BASE+3u)
#define OG_REC_SPS    (OG_REC_BASE+4u)

u64 stock_og_nraw_header(u64,u64,u64,u64,u64,u64,u64,u64);  /* 0x4109E61C NRAW header builder */
u64 stock_og_nctg_height(u64,u64,u64,u64,u64,u64,u64,u64);  /* 0x40706FA8 NCTG height getter */
u64 stock_og_nctg_proxy(u64,u64,u64,u64,u64,u64,u64,u64);   /* 0x40707404 NCTG proxy main-size writer */
u64 stock_og_nctg_crop(u64,u64,u64,u64,u64,u64,u64,u64);    /* 0x40707D28 NCTG crop tag getter */
u64 stock_og_sps(u64,u64,u64,u64,u64,u64,u64,u64);          /* 0x40CEF168 avcC sample-entry writer */

static u32 og_rec_kind(u64 p){return og_plan_kind(p);}
static inline u32 rb8(u64 a){return *(volatile const u8 *)(uintptr_t)a;}
static inline void wb8(u64 a,u32 v){*(volatile u8 *)(uintptr_t)a=(u8)v;}
static inline u32 rbe32(u64 a){return rb8(a)<<24|rb8(a+1)<<16|rb8(a+2)<<8|rb8(a+3);}
static inline u32 rle16(u64 a){return rb8(a)|rb8(a+1)<<8;}
static inline void wle16(u64 a,u32 v){wb8(a,v);wb8(a+1,v>>8);}
static inline u32 rle32(u64 a){return rle16(a)|rle16(a+2)<<16;}
#define OG_FOURCC(a,b,c,d) ((u32)(a)<<24|(u32)(b)<<16|(u32)(c)<<8|(u32)(d))

/* ---- 2.5 the NRAW file header (container) ----
 * 0x4109E61C(w0, x1 = writer context, x2, x3 = out): for w0 == 4 it fills a 0x400-byte ring
 * buffer (out +0x18, size out +0x20) with NRAW{NRFM(12) NRFH{NRHM(12) 20 x NRMT(u32 BE size,
 * "NRMT", u32 BE tag, value)} NRTH ...}. The old layout's value sites ran inside it:
 *   NRMT 0x01100101 (fn 0x4109EF0C, site 0x4109EF34): value 00 00 00 hi lo = coded rows;
 *   NRMT 0x0190000B (fn 0x4109F6A8, sites F700/F718 into 0x41189690(w, h)): 00 a a 04 04, a = 4
 *     only for 4040 x 2276, else 6 (so 6K is unchanged, 4K becomes 6 at 2692 rows);
 *   NRMT 0x02000001 (fn 0x4109F9C8): 00 'I' 'I' then 13 IFD sub-records (u16 tag, u16 type,
 *     u32 count, count x size(type) bytes); tag 0x45 (fn 0x410A0350, sites A040C-A043C) holds
 *     u16 LE (a, 4, active width, active rows).
 * The walk checks every size against its parent and must end exactly at the parent's end; the
 * three entries must all hold the stock values of the kind's path, else nothing is edited. */
#define OG_NRFH_MAX 24u          /* NRHM + 20 NRMT in stock; bound of the unrolled walk */
#define OG_IFD_MAX  16u          /* 13 sub-records in stock */
static u32 og_ifd_size(u32 type) {
    if(type==1u||type==2u||type==6u||type==7u)return 1;
    if(type==3u||type==8u)return 2;
    if(type==4u||type==9u||type==11u)return 4;
    if(type==5u||type==10u||type==12u)return 8;
    return 0;
}
/* The walks are straight-line: one step function called a fixed number of times (the verifier
 * accepts no loop; clang cannot fully unroll a loop with early exits). A step does nothing once
 * the walk has ended or failed. */
struct og_walk {u64 at,end,rows,ratio,ifd;u32 seen,bad;};
#define OG_REP4(x) x;x;x;x
#define OG_REP8(x) OG_REP4(x);OG_REP4(x)
/* One IFD sub-record (u16 tag, u16 type, u32 count, data): tag 0x45 noted in w->ifd. */
__attribute__((noinline)) static void og_ifd_step(struct og_walk *w) {
    if(w->bad||w->at==w->end)return;
    if(w->end-w->at<8u){w->bad=1;return;}
    u32 tag=rle16(w->at),type=rle16(w->at+2),count=rle32(w->at+4),size=og_ifd_size(type);
    if(!size||count>0x400u||count*size>w->end-w->at-8u){w->bad=1;return;}
    if(tag==0x45u) {
        if(w->ifd||type!=3u||count!=4u){w->bad=1;return;}
        w->ifd=w->at+8u;
    }
    w->at+=8u+count*size;
}
/* The IFD tag 0x45 data in value v (n bytes) of NRMT 0x02000001, or 0. */
__attribute__((noinline)) static u64 og_ifd_45(u64 v,u32 n) {
    if(n<3u||rb8(v)!=0u||rb8(v+1)!='I'||rb8(v+2)!='I')return 0;
    struct og_walk w={v+3u,v+n,0,0,0,0,0};
    OG_REP8(og_ifd_step(&w));OG_REP8(og_ifd_step(&w));      /* OG_IFD_MAX = 16 */
    return !w.bad&&w.at==w.end?w.ifd:0;
}
/* One NRFH child box: the three NRMT entries noted. */
__attribute__((noinline)) static void og_nrfh_step(struct og_walk *w) {
    if(w->bad||w->at==w->end)return;
    if(w->end-w->at<8u){w->bad=1;return;}
    u32 size=rbe32(w->at);
    if(size<8u||size>w->end-w->at){w->bad=1;return;}
    if(rbe32(w->at+4)==OG_FOURCC('N','R','M','T')) {
        if(size<12u){w->bad=1;return;}
        u32 tag=rbe32(w->at+8),n=size-12u;
        u64 v=w->at+12u;
        if(tag==0x01100101u){if((w->seen&1u)||n!=5u){w->bad=1;return;}w->seen|=1u;w->rows=v;}
        else if(tag==0x0190000bu){if((w->seen&2u)||n!=5u){w->bad=1;return;}w->seen|=2u;w->ratio=v;}
        else if(tag==0x02000001u){if(w->seen&4u){w->bad=1;return;}w->seen|=4u;w->ifd=og_ifd_45(v,n);if(!w->ifd){w->bad=1;return;}}
    }
    w->at+=size;
}
static u32 og_nraw_fix(u64 buf,u32 k) {
    if(rbe32(buf)!=0x400u||rbe32(buf+4)!=OG_FOURCC('N','R','A','W'))return 0;
    if(rbe32(buf+8)!=12u||rbe32(buf+12)!=OG_FOURCC('N','R','F','M'))return 0;
    u32 fh=rbe32(buf+0x14);
    if(rbe32(buf+0x18)!=OG_FOURCC('N','R','F','H')||fh<8u||fh>0x400u-0x14u)return 0;
    struct og_walk w={buf+0x1cu,buf+0x14u+fh,0,0,0,0,0};
    OG_REP8(og_nrfh_step(&w));OG_REP8(og_nrfh_step(&w));OG_REP8(og_nrfh_step(&w));   /* OG_NRFH_MAX = 24 */
    if(w.bad||w.at!=w.end||w.seen!=7u)return 0;
    u64 rows=w.rows,ratio=w.ratio,ifd=w.ifd;
    /* stock values of the kind's path (received geometry) */
    u32 h=rb8(rows+3)<<8|rb8(rows+4);
    u32 a=rb8(ratio+1);
    u32 ok=!rb8(rows)&&!rb8(rows+1)&&!rb8(rows+2)&&!rb8(ratio)&&rb8(ratio+2)==a&&rb8(ratio+3)==4u&&rb8(ratio+4)==4u&&
           rle16(ifd+2)==4u;
    if(k==6u)ok=ok&&h==3410u&&a==6u&&rle16(ifd)==6u&&rle16(ifd+4)==6048u&&rle16(ifd+6)==3402u;
    else ok=ok&&k==4u&&h==2276u&&a==4u&&rle16(ifd)==4u&&rle16(ifd+4)==4032u&&rle16(ifd+6)==2268u;
    if(!ok)return 0;
    u32 open=k==6u?4040u:2692u;
    wb8(rows+3,open>>8);wb8(rows+4,open);
    if(k==4u){wb8(ratio+1,6);wb8(ratio+2,6);wle16(ifd,6);}
    wle16(ifd+6,k==6u?4032u:2688u);
    return k;
}
u64 g10_nraw_header(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u64 result=stock_og_nraw_header(OG_PASS);
    if(!ph||(u32)a0!=4u||!a3)return result;
    u32 k=og_rec_kind(p);
    if(!k)return result;
    u64 buf=z63g_ld64(a3+0x18);
    u32 done=buf&&z63g_ld32(a3+0x20)==0x400u?og_nraw_fix(buf,k):0u;
    og_put8(p,OG_REC_NRAW,done);
    return result;
}

/* ---- 2.6 NCTG descriptor words ----
 * Height getter 0x40706FA8(x0 = context, x1 = record) -> 1: proxy flag byte +0xEC clear, mode
 * +0xC (<= 7) through table 0x441BB19A: mode 6 is the old site 0x40707028 (3402), mode 4 the old
 * site 0x40707018 (2268); the value goes to record +0xA6. Inputs are read before the stock call. */
u64 g10_nctg_height(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u32 flag=1,mode=0;
    if(ph&&a0&&a1){flag=rb8(a0+0xec);mode=z63g_ld32(a0+0xc);}
    u64 result=stock_og_nctg_height(OG_PASS);
    if(!ph||!a0||!a1||flag||(u32)result!=1u||(mode!=6u&&mode!=4u))return result;
    u32 k=og_rec_kind(p);
    if(!k)return result;
    u32 done=0;
    if(k==6u&&mode==6u&&og_ld16(a1+0xa6)==3402u){*(volatile u16 *)(uintptr_t)(a1+0xa6)=4032;done=6;}
    else if(k==4u&&mode==4u&&og_ld16(a1+0xa6)==2268u){*(volatile u16 *)(uintptr_t)(a1+0xa6)=2688;done=4;}
    og_put8(p,OG_REC_HEIGHT,done);
    return result;
}
/* Proxy main-size writer 0x40707404(x0 = context, x1 = record) -> 1: flag +0xEC set, +0x24 3 or
 * 4, (+0x2C & ~1) == 2, +0xC - 2 through table 0x441BB1B8: 4 is the old site 0x40707500 (6048 x
 * 3402), 2 the old site 0x407074E8 (4032 x 2268); (w, h) go to record +0x102 / +0x104. */
u64 g10_nctg_proxy(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u32 flag=0,f24=0,f2c=0,mode=0;
    if(ph&&a0&&a1){flag=rb8(a0+0xec);f24=z63g_ld32(a0+0x24);f2c=z63g_ld32(a0+0x2c);mode=z63g_ld32(a0+0xc);}
    u64 result=stock_og_nctg_proxy(OG_PASS);
    if(!ph||!a0||!a1||!flag||(u32)result!=1u||(f24!=3u&&f24!=4u)||(f2c&~1u)!=2u||(mode!=6u&&mode!=4u))return result;
    u32 k=og_rec_kind(p);
    if(!k)return result;
    u32 done=0;
    if(k==6u&&mode==6u&&og_ld16(a1+0x102)==6048u&&og_ld16(a1+0x104)==3402u){*(volatile u16 *)(uintptr_t)(a1+0x104)=4032;done=6;}
    else if(k==4u&&mode==4u&&og_ld16(a1+0x102)==4032u&&og_ld16(a1+0x104)==2268u){*(volatile u16 *)(uintptr_t)(a1+0x104)=2688;done=4;}
    og_put8(p,OG_REC_PROXY,done);
    return result;
}
/* Crop tag 0x0200001B getter 0x40707D28(x0 = context, x1 = record) -> 1: the descriptor helper
 * writes seven u16 at record +0x336 (kind, full w, h, crop w, h, x, y; FX 16:9 stock
 * (6, 6064, 4040, 6064, 3416, 0, 312)). The main file of an open-gate recording (proxy flag
 * +0xEC clear) gets the firmware's FX-uncropped kind 11 and the full rectangle, for 6K and 4K
 * alike (old z63_og_crop_tag; the old adapter skipped the helper, this one runs it and then
 * writes the same seven values). */
u64 g10_nctg_crop(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u32 flag=ph&&a0?rb8(a0+0xec):1u;
    u64 result=stock_og_nctg_crop(OG_PASS);
    if(!ph||!a0||!a1||flag||(u32)result!=1u)return result;
    u32 k=og_rec_kind(p);
    if(!k)return result;
    volatile u16 *c=(volatile u16 *)(uintptr_t)(a1+0x336);
    c[0]=11;c[1]=6064;c[2]=4040;c[3]=6064;c[4]=4040;c[5]=0;c[6]=0;
    og_put8(p,OG_REC_CROP,k);
    return result;
}

/* ---- 2.7 proxy SPS ----
 * The avcC sample-entry writer 0x40CEF168(x0 = context, x1 = SPS, w2 = SPS length, x3 = PPS,
 * w4 = PPS length) copies the SPS bytes into the entry and keeps no pointer. For an open-gate
 * recording's 1920 x 1080 proxy (context state +0 1 or 2, +0x518 1920, +0x51C 1080) with one of
 * the three known stock proxy SPS (29.97p, 25p, 23.976p), the writer gets the old layout's
 * corrected SPS instead (27:32 sample aspect ratio: 1920 x 1080 shown as 3:2; no timing VUI),
 * from this adapter's frame. Everything else, including the PPS, is the caller's. */
static u64 og_le64(u64 a) {
    u64 v=0;
    #pragma clang loop unroll(full)
    for(u32 i=0;i<8u;i++)v|=(u64)rb8(a+i)<<(8u*i);
    return v;
}
static u32 og_sps_stock(u64 s) {
    if(og_le64(s)!=0xe001d3ac2a006467ull||og_le64(s+8)!=0x800202026e969f08ull)return 0;
    u64 t=og_le64(s+16);
    return t==0x4230750080f40100ull||t==0x42a8610000f40100ull||t==0x42c05d0080f40100ull;
}
u64 g10_proxy_sps(OG_ARGS) {
    u64 p=z63g_page();
    u32 ph=p?z63g_phase(p,G10):0;
    u64 sar[3];
    u64 sps=a1;
    if(ph&&a0&&a1&&(u32)a2==24u) {
        u32 state=z63g_ld32(a0);
        if((state==1u||state==2u)&&z63g_ld32(a0+0x518)==1920u&&z63g_ld32(a0+0x51c)==1080u&&og_sps_stock(a1)) {
            u32 k=og_rec_kind(p);
            if(k) {
                sar[0]=0xe001d3ac2a006467ull;sar[1]=0x20001b00ff979f08ull;sar[2]=0x96311c0f0202026eull;
                sps=(u64)(uintptr_t)sar;
            }
            og_put8(p,OG_REC_SPS,k);
        }
    }
    return stock_og_sps(a0,sps,a2,a3,a4,a5,a6,a7);
}
