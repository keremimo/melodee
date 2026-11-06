/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The sampled kits' encoder (tools/drum_kit_make.py runs it; firmware/src/drum_real.c decodes). A 4-tap sign-LMS
 * predictor after QOA (qoaformat.org, its specification is CC0), with a bit depth per slice:
 *   64-bit slice, big end first: [2 mode][4 scale][58 residuals]; mode 0: 58 x 1 bit, 1: 29 x 2, 2: 19 x 3, 3: 14 x 4
 * Each slice takes the fewest bits that keep the coding noise snr_db below that slice's own level.
 *   drum_codec in.s16 out.bin dec.s16 snr_db emphasis_q8 loop_at [nf1_q8 nf2_q8]     (loop_at 0: no loop)
 * prints: samples bytes loop_slice_sample loop_byte h0..h3 w0..w3 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static const int SF[16]={1,2,3,6,11,20,36,66,120,218,398,724,1319,2400,4370,7960};   /* wider than QOA's: a hat's attack steps 38000 in a sample */
static const int NS[4]={58,29,19,14};
static const int BITS[4]={1,2,3,4};
static const int8_t LV[4][16]={          /* levels, Q2 */
    {5,-5},
    {3,-3,10,-10},
    {3,-3,10,-10,18,-18,28,-28},
    {2,-2,6,-6,10,-10,14,-14,18,-18,22,-22,26,-26,30,-30}};
typedef struct {int32_t h[4],w[4];} lms_t;
static inline int32_t pred(const lms_t *l){uint32_t p=0;for(int i=0;i<4;i++)p+=(uint32_t)l->h[i]*(uint32_t)l->w[i];return (int32_t)p>>13;}
static inline void upd(lms_t *l,int32_t s,int32_t r){int32_t d=r>>4;for(int i=0;i<4;i++)l->w[i]+=l->h[i]<0?-d:d;l->h[0]=l->h[1];l->h[1]=l->h[2];l->h[2]=l->h[3];l->h[3]=s;}
static inline int32_t cl(int32_t v){return v<-32768?-32768:v>32767?32767:v;}
static int NF1,NF2;   /* noise feedback, Q8: the noise leaves through 1 + NF1 z^-1 + NF2 z^-2 */
typedef struct {int32_t e1,e2;} nf_t;
static double slice(const lms_t *st,const int16_t *x,int n,int m,int sf,uint64_t *code,lms_t *out,int16_t *rec,nf_t *nf,double *pure)
{
    lms_t l=*st;nf_t q=*nf;double e=0,ep=0;uint64_t c=0;int L=1<<BITS[m];int sc=SF[sf];
    for(int i=0;i<n;i++){
        int32_t p=pred(&l),br=0,bk=0;double bd=1e30;
        int32_t tg=x[i]+((NF1*q.e1+NF2*q.e2)>>8);tg=cl(tg);
        for(int k=0;k<L;k++){int32_t r=(LV[m][k]*sc)>>2;double d=fabs((double)tg-(double)cl(p+r));if(d<bd){bd=d;bk=k;br=r;}}
        int32_t s=cl(p+br);double er=(double)tg-s;e+=er*er;ep+=er*er;q.e2=q.e1;q.e1=s-tg;if(q.e1>6000)q.e1=6000;if(q.e1<-6000)q.e1=-6000;
        long long wp=0;for(int k=0;k<4;k++)wp+=(long long)l.w[k]*l.w[k];
        if(wp>0x2fffffffLL)e+=(double)((wp>>18)-0xbff);
        c=(c<<BITS[m])|(uint64_t)bk;upd(&l,s,br);if(rec)rec[i]=(int16_t)s;
    }
    *code=c;*out=l;*nf=q;if(pure)*pure=ep;return e;
}
int main(int ac,char **av)
{
    if(ac<7)return 2;
    if(ac>7)NF1=atoi(av[7]);if(ac>8)NF2=atoi(av[8]);
    FILE *f=fopen(av[1],"rb");if(!f)return 2;fseek(f,0,SEEK_END);int N=(int)(ftell(f)/2);fseek(f,0,SEEK_SET);
    int16_t *x=calloc(N+64,2);if(fread(x,2,N,f)!=(size_t)N)return 2;fclose(f);
    double snr=atof(av[4]);int em=atoi(av[5]),la=atoi(av[6]);
    if(em>0){int32_t prev=0;for(int i=0;i<N;i++){int32_t c=x[i];x[i]=(int16_t)cl(c-((em*prev)>>8));prev=c;}}
    FILE *o=fopen(av[2],"wb");int16_t *d=calloc(N+128,2);lms_t st;memset(&st,0,sizeof st);st.w[2]=-(1<<13);st.w[3]=1<<14;
    int at=0,bytes=0,lat=0,lb=0;lms_t ls=st;nf_t nq={0,0};
    while(at<N){
        if(la&&!lat&&at>=la){lat=at;lb=bytes;ls=st;}
        int chosen=3,bsf=0;uint64_t code=0;
        for(int m=0;m<4;m++){
            int n=NS[m];double en=0;for(int i=0;i<n;i++)en+=(double)x[at+i]*x[at+i];
            double target=en*pow(10,-(snr+(at<176?10.0:0.0))/10),fl=n*1.5*1.5;   /* the first 4 ms: 10 dB more */
            if(target<fl)target=fl;
            double me=1e300,mp=0;uint64_t mc=0;int ms=0;
            for(int s=0;s<16;s++){uint64_t c;lms_t t;nf_t tq=nq;double ep;double e=slice(&st,x+at,n,m,s,&c,&t,NULL,&tq,&ep);if(e<me){me=e;mp=ep;mc=c;ms=s;}}
            chosen=m;code=mc;bsf=ms;if(mp<=target)break;
        }
        int n=NS[chosen];uint64_t w=((uint64_t)chosen<<62)|((uint64_t)bsf<<58)|(code<<(58-n*BITS[chosen]));
        unsigned char b[8];for(int i=0;i<8;i++)b[i]=(unsigned char)(w>>(56-8*i));fwrite(b,1,8,o);bytes+=8;
        uint64_t cc;lms_t tt;slice(&st,x+at,n,chosen,bsf,&cc,&tt,d+at,&nq,NULL);st=tt;at+=n;
    }
    fclose(o);
    if(em>0){int32_t y=0;for(int i=0;i<N;i++){y=d[i]+((em*y)>>8);d[i]=(int16_t)cl(y);}}
    FILE *r=fopen(av[3],"wb");fwrite(d,2,N,r);fclose(r);
    printf("%d %d %d %d %d %d %d %d %d %d %d %d\n",N,bytes,lat,lb,ls.h[0],ls.h[1],ls.h[2],ls.h[3],ls.w[0],ls.w[1],ls.w[2],ls.w[3]);
    return 0;
}
