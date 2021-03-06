// test file compression based on lzoma algorith
// (c) 2015 Alexandr Efimov
// License: GPL 2.0 or later
// Notes:
//
// Pros:
// Compression ratio is very good (much higher than lzo, ucl, gzip, bzip2, somewhat higher than lzham. Only a bit worse than lzma/xz).
// Decompression speed is very high (faster than gzip, much faster than bzip,lzham, lzma,xz)
// tiny decompressor code (asm version of decomress function less than 400 bytes)
//
// compressed data format is somewhere between lzo and lzma
// uses static encoding and byte-aligned literals and byte-aligned parts of match offset for decompression speed
//
// Cons:
// compressor is VERY slow. It is possible to implement faster compressor at the cost of some compression ratio.
// may be it is possible to adapt lzma compressor code
//
// code of both compression/decompression utils is only experimental. no support for streaming, can only compress one file that is fully loaded in memory.
// compressed data format is nowhere near stable yet.
//
#include <stdio.h>
#include <stdlib.h>

#define byte unsigned char
#define MAX_SIZE 16384*1024
//0x52398c
// min lz is 1bit+10bits+breakbit+2bitlen=14 bit. 2 chars=18bit
#define shortlen 2048
#define longlen 32768
#define breaklz 256
//1024
#define breaklen 4
// level 5 / match_level 10000 compresses better but many times slower
#define level 3
#define match_level 1000
//#define level 5
//#define level 1
//#define match_level 10000000
//#define match_level 2

byte in_buf[MAX_SIZE]; /* text to be encoded */
byte out_buf[MAX_SIZE];
byte out_best[MAX_SIZE];

int use_olz[MAX_SIZE];
int olz_len[MAX_SIZE];
//int best_ofs[MAX_SIZE]; /* best way to start, assuming we do not start with OLD OFFSET code */
//int best_len[MAX_SIZE]; /* best way to start, assuming we do not start with OLD OFFSET code */
//int cache[MAX_SIZE]; /* best possible result if we start with lz or letter code */
#define cache same
#define best_ofs same3
#define best_len samelen
int same[MAX_SIZE];
int same3[MAX_SIZE];
//int gen_same[65536];
#define gen_same same3
int samelen[MAX_SIZE];
int rle[MAX_SIZE+1];
int cacherle[MAX_SIZE+1];

int sorted[MAX_SIZE];
int pos2sorted[MAX_SIZE];
int sorted_len[MAX_SIZE];
int sorted_prev[MAX_SIZE];
int sorted_next[MAX_SIZE];

static inline int len_encode(int num,int total) {
  register int res=0;
  register int x=1;
  register int ox;
  int break_at=breaklz;

  if (total>=256) {
    if (num<128) return 8;
    x=128;
    res=7;
  }

  while (1) {
    ox=x;
    x+=x;
    if (x>=total) break; /* only 1 bit to be outputted left */
    if (x>=break_at) {
      res++; /* we write 'break' bit */
      num-=ox;
      if (num<0) goto doneit;
      total-=ox;
      break_at<<=3;//2;
    }
    res++;
  }
  if (num>=x-total) { res++;}
doneit: 
  return res;
//  }
}

static inline int len_encode_l(int num) {//num>=2
if (num<4) return 2;// 00 01
num-=2;
  //return 1+((31-__builtin_clz(num))<<1);
  if (num<4) return 1+2;// 0 0=2 1=3
  if (num<8) return 1+4; // 10 00=4 01=5 10=6 11=7
  if (num<16) return 1+6; // 110 xxx 8-15
  if (num<32) return 1+8;
  if (num<64) return 1+10;
  if (num<128) return 1+12;
  if (num<256) return 1+14;
  if (num<512) return 1+16;
  if (num<1024) return 1+18;
  if (num<2048) return 1+20;
  if (num<4096) return 1+22;
  if (num<8192) return 1+24;
  if (num<16384) return 1+26;
  if (num<32768) return 1+28;
  if (num<65536) return 1+30;
  if (num<131072) return 1+32;
  if (num<262144) return 1+34;
  if (num<524288) return 1+36;
  return 1+38;
}

static inline int len_encode_l_dec(int num) {//num>=2
  if (num<4) return num;// 0 0=2 1=3
  if (num<8) return num-3; // 10 00=4 01=5 10=6 11=7
  if (num<16) return num-7; // 110 xxx 8-15
  if (num<32) return num-15;
  if (num<64) return num-31;
  if (num<128) return num-63;
  if (num<256) return num-127;
  if (num<512) return num-255;
  if (num<1024) return num-511;
  if (num<2048) return num-1023;
  if (num<4096) return num-2047;
  if (num<8192) return num-4095;
  if (num<16384) return num-8191;
  if (num<32768) return num-16383;
  if (num<65536) return num-32767;
  if (num<131072) return num-65535;
  if (num<262144) return num-131071;
  if (num<524288) return num-262143;
  return num-524287;
}


int lastpos;
int bit_cnt;
int outpos;

static inline void putbit(int bit) {
  bit_cnt>>=1;
  if (bit_cnt==0) {
    lastpos=outpos;
    out_buf[lastpos]=0;
    outpos++;
    bit_cnt=128;
  }
  if (bit) out_buf[lastpos]|=bit_cnt;
}

static inline void putenc(int num,int total, int break_at) {
  char bits[100];
  int res=0;
  int x=1;
  int obyte=0;
if (total >= 256 && break_at >= 256)
obyte=1;
//  printf("debug: putenc num=%d, total=%d, break_at=%d,",num,total,break_at);

  while (1) {
    x+=x;
    if (x>=total) break; /* only 1 bit to be outputted left */
    if (x>=break_at) {
      if (num<(x>>1)) {bits[res++]=0;  goto doneit;}
      bits[res++]=1;
      num-=x>>1;        
      total-=x>>1;
      if (break_at!=breaklen) break_at<<=3;
    }
    bits[res++]=2;
  }
  if (num>=x-total) {
    if (num>=(x>>1)) {
      bits[res++]=1; num-=total-(x>>1);
    } else {
      bits[res++]=0;
    }
  }

doneit: 
  for(x=res-1;x>=0;x--) {
    if (bits[x]==2) {
      bits[x]=num&1;
      num>>=1;
    }
  }
  if (obyte) {
  byte b=0;
  for(x=0;x<8;x++) 
    if (bits[x]) b|=128>>x;
  out_buf[outpos++]=b;
  for(;x<res;x++) 
    putbit(bits[x]);
  }
  else 
  for(x=0;x<res;x++) {
    putbit(bits[x]);
  }
}

int old_ofs=0;
int was_letter=1;

byte lb;
void initout(void) {
  lb=out_buf[0]=in_buf[0];
  outpos=1;
  bit_cnt=1;
  old_ofs=0;
  was_letter=1;
}

int stlet=0;
int stlet7=0;
int stlz=0;
int stolz=0;
static inline void put_lz(int offset,int length,int used,int len_left) {
  putbit(1);
  offset=-offset; /* 1.. */
  offset--; /* 0.. */

//printf("was %d old %d\n",was_letter, old_ofs==offset? 1 : 0);
printf(was_letter?"w":"n");
  if (was_letter) {
    was_letter=0;
    if (old_ofs==offset) {
    stolz++;
      putbit(0);
      length-=2;
      printf("o%d\n",length);
      if (length==0) { putbit(0);putbit(0);}
      else
      if (length==1) { putbit(0);putbit(1);}
      else {putbit(1);
      putenc(length-2,len_left-1,breaklen);}
      return;
    }
    putbit(1);
  }
  stlz++;
  if (offset+1>=longlen) { length--; }
      length-=2;
      printf("l%d %d\n",length,offset);
      putenc(offset,used,breaklz);
      if (length==0) { putbit(0);putbit(0);}
      else
      if (length==1) { putbit(0);putbit(1);}
      else {putbit(1);
  putenc(length-2,len_left-1,breaklen);}
 
  old_ofs=offset;
}

static inline void put_letter(byte b) {
  printf("i\n");
  putbit(0);
  out_buf[outpos++]=b;
  //fprintf(stderr,"%c",b-lb);
  was_letter++;
  stlet++;
  b-=lb;
  b+=64;
  if (b<128) stlet7++;
  b-=64;
  b+=lb;
  lb=b;
}

#define MIN_RUN 4
void put_run(int i, int lit) {//min run is 6 or 3 or 4 lets try...
stlet+=lit;
was_letter=1;
  int l=lit;
  if (l<MIN_RUN) l=MIN_RUN; // rest of input only
  l-=MIN_RUN;
  if (l<15) {
    out_buf[outpos++]=(byte)l;
  } else {
    out_buf[outpos++]=15;
    l-=15;
    for(;;) {
      if (l<255) {
        out_buf[outpos++]=(byte)l;
        break;
      }
      out_buf[outpos++]=255;
      l-=255;
    }
  }
  while(lit--) out_buf[outpos++]=in_buf[i++];
}

void put_olz(int i, int lit, int len) { // lit 1..MIN_RUN; len >=2
  len-=2;
  printf("o%d\n",len);
  stlet+=lit;
  stolz++;
  byte b=lit-1;
  if (len<11) {
    out_buf[outpos++]=16+b+len;
  } else {
    out_buf[outpos++]=16+b+11;
    len-=11;
    for(;;) {
      if (len<255) {
        out_buf[outpos++]=(byte)len;
        break;
      }
      out_buf[outpos++]=255;
      len-=255;
    }
  }
  while(lit--) out_buf[outpos++]=in_buf[i++];
}

void short_match(int offset,int len) // offset 0..2047, len 0..3
{
  outpos+=2;
}

void match3b(int offset,int len)
{
  outpos+=3;
  if (len>=3+15) outpos++;
}

void match4b(int offset,int len)
{
  outpos+=4;
  if (len>=4+15) outpos++;
}

void put_match(int i, int lit, int offset, int len) { // lit 0..3; 
  offset=-offset;
  offset--;
  if (lit) was_letter=1;
  printf(was_letter?"w":"n");
  if (was_letter && old_ofs==offset) return put_olz(i,lit,len);
  len-=2;
  if (offset+1>=longlen) { len--; }
  printf("l%d %d\n",len, offset);
    old_ofs=offset;
  stlet+=lit;
  stlz++;
  if (len<=3 && offset<2048) {
    short_match(offset,len);
  } else if (offset < 32768) {
    match3b(offset,len);
  } else {
    match4b(offset,len);
  }
  /*
  byte b=lit;
  byte l=len;
  if (l>5) {l=5;len-=5;}
  int ofs=offset;
  if (ofs>2047) { ofs=2047; offset-=2047; }
  b+=l<<5;
  b+=(ofs>>8)<<2;
  b+=64;
  out_buf[outpos++]=b;
  out_buf[outpos++]=ofs&0xff;
  if (l==5) {
    for(;;) {
      if (len<255) {
        out_buf[outpos++]=(byte)len;
        break;
      }
      out_buf[outpos++]=255;
      len-=255;
    }
  }
  if (ofs==2047) {
    for(;;) {
      if (offset<65535) {
        out_buf[outpos++]=(byte)offset;
        out_buf[outpos++]=(byte)(offset>>8);
        break;
      }
      out_buf[outpos++]=65535;
      offset-=65535;
    }
  }*/
  was_letter=0;
  while(lit--) out_buf[outpos++]=in_buf[i++];
}

static inline int len_lz(int offset, int length, int used) { // offset>=1, length>=2, 
                                                    // if offset=>0xD00  length>=3
  int res=0;
  length-=2;
  offset--;
  if (offset < shortlen && length<=3) return 2;
  length--;
  if (offset <shortlen) length--;
  if (offset <longlen) length--;
  if (offset <longlen) res=3;
  else res=4;
  if (length>=15) res++;

  return res;
}

static inline int len_olz_minus_lz(int offset, int length, int used) { // offset>=1, length>=2, 
                                                    // if offset=>0xD00  length>=3
  int res=1;
  if (offset>=longlen) { res+=len_encode_l(length)-len_encode_l(length-1); }
  offset--; /* 0.. */
  return res-len_encode(offset,used);
}

static inline int cmpstr(int src,int src2) {
  int res=0;
  int b;
  
//  printf("%d:%d:%d\n",src,src2,left);
  for(;;) {
    if (in_buf[src]!=in_buf[src2]) return res;
    b=rle[src2];
    if (!b) return res;
    if (b>rle[src]) {return res+rle[src];}
    res+=b;
    src+=b;
    src2+=b;
  }
  return res;
}

int cmpstrsort(int *psrc,int *psrc2) {
  int b;
  int src = *psrc;
  int src2 = *psrc2;
//  printf("%d:%d:%d\n",src,src2,left);
  do {
    if (in_buf[src]<in_buf[src2]) return -1;
    if (in_buf[src]>in_buf[src2]) return 1;
    b=rle[src2];
    if (!b) return 1; // first string is longer
    if (b>rle[src]) b=rle[src];
    if (!b) return -1; // second string is longer
    src+=b;
    src2+=b;
  } while(1);
}

void init_same(int n) {
  int i;
  unsigned short int bb;
  int old;

  old = 1;
  rle[n-1]=old;
  byte b = in_buf[n-1];
  for(i=n-2;i>=0;i--) {
    if (in_buf[i]==b) 
      {/*if (old<255)*/ old++;}
    else {
      b=in_buf[i];
      old = 1;
    }
    rle[i]=old;
  }
  rle[n]=0;

  bb=0;old=0;

  for(i=0;i<65536;i++) {gen_same[i]=-1-longlen; }
  for(i=0;i<n-1;i++) { 
    cache[i]=0;
    bb=in_buf[i]; bb<<=8; bb|=in_buf[i+1];
    same[i]=gen_same[bb]; 
    if (gen_same[bb]>=0) { samelen[i]=1+cmpstr(i+1,same[i]+1);}
    gen_same[bb]=i; 
  }
  same[i]=-1;

  printf("init_same done.\n");

  for(i=0;i<n;i++) sorted[i]=i;

  qsort( sorted, n, sizeof( int ),
               ( int (*)(const void *, const void *) ) cmpstrsort );
  printf("qsort done.\n");

  for(i=0;i<n;i++) pos2sorted[sorted[i]]=i;
  
  for(i=0;i<n-1;i++) sorted_len[i]=cmpstr(sorted[i],sorted[i+1]);
  sorted_len[n-1]=0;

  sorted_prev[0]=-1;
  for(i=1;i<n;i++) sorted_prev[i]=sorted_len[i-1] >= 2? i-1:-1;

  for(i=0;i<n-1;i++) sorted_next[i]=sorted_len[i] >= 2? i+1:-1;
  sorted_next[n-1]=-1;
  in_buf[n]=0;
  
  printf("sort done.\n");
}

static inline int max(int a,int b) {
  return a>b? a:b;
}

static inline int min(int a,int b) {
  return a<b? a:b;
}

#define CHECK_OLZ \
        int k;\
        int d=level;\
        int tmp=len_lz(used-pos,len,used);\
        int olen=0;\
        for(k=len+1;k<left-2;k++) {\
          tmp++;\
	  if (k==5) tmp++;\
          if (0&&best_ofs[used+k]==pos-used) {\
            int tmp2=tmp+len_olz_minus_lz(used-pos,best_len[used+k],used+k);\
            tmp2+=cache[used+k];\
            if (tmp2<res) {\
              res=tmp2;\
              my_best_ofs=pos-used;\
              my_best_len=len;\
              my_use_olz=k-len;\
              my_olz_len=best_len[used+k];\
            }\
          }\
          if (olen==0) {\
            olen=cmpstr(used+k,pos+k);\
            for (j=2;j<olen;j+=cacherle[used+k+j]) {\
              int tmp2=tmp+1;\
              tmp2+=cache[used+k+j];\
              if (tmp2<=res) {\
                res=tmp2;\
                my_best_ofs=pos-used;\
                my_best_len=len;\
                my_use_olz=k-len;\
                my_olz_len=j;\
              }\
            }\
	    if (olen>1) {\
              int tmp2=tmp+1;\
              tmp2+=cache[used+k+olen];\
              if (0&&best_len[used+k+olen]==1) {\
	        int jj;\
	        for(jj=1;jj<=8;jj++) {\
                  if (best_len[used+k+olen+jj]>1) {\
                    if (best_ofs[used+k+olen+jj]==pos-used) {\
                      tmp2+=len_olz_minus_lz(used-pos,best_len[used+k+olen+jj],used+k+olen+jj);\
                    }\
	            break;\
	          }\
	        }\
              }\
              if (tmp2<=res) {\
                res=tmp2;\
                my_best_ofs=pos-used;\
                my_best_len=len;\
                my_use_olz=k-len;\
                my_olz_len=olen;\
              }\
	    }\
          } else olen--;\
          if (best_ofs[used+k]) {\
            d--; if (d==0) break;\
          }\
        }


int pack(int n) {
  int res;
  int i;

  if (n==0) { return 0; }

  init_same(n);

  cache[n-1]=2; /* last letter cannot be packed as a lz */
  cacherle[n-1]=1;
  cacherle[n]=1;
  best_ofs[n-1]=0;
  best_len[n-1]=1;
  use_olz[n-1]=0;

  if (sorted_prev[pos2sorted[n-1]]>=0) {
    sorted_len[sorted_prev[pos2sorted[n-1]]] = min(sorted_len[sorted_prev[pos2sorted[n-1]]],
                                            sorted_len[pos2sorted[n-1]]);
    sorted_next[sorted_prev[pos2sorted[n-1]]]=sorted_next[pos2sorted[n-1]];
  }
  if (sorted_next[pos2sorted[n-1]]>=0) sorted_prev[sorted_next[pos2sorted[n-1]]]=sorted_prev[pos2sorted[n-1]];

  for(i=n-2;i>0;i--) {
    int used=i;
    int left=n-i;
    int res;
    int pos;
    int max_match;
    int len;
    int j;

    int my_best_ofs=0;
    int my_best_len=1;
    int my_use_olz=0;
    int my_olz_len=0;
    int match_check_max = match_level;
    int notskip = 1; 

    res=1+cache[used+1];
    if (!best_ofs[used+1] && !best_ofs[used+2]
        && !best_ofs[used+3]) res++; // have to code run
    if (best_ofs[used+1]) {
      //res++;
      if (in_buf[used]==in_buf[used+1]) {
        if (in_buf[used]==in_buf[used-1]) {
          if (in_buf[used]==in_buf[used+best_ofs[used+1]]) {
            if ((best_len[used+1]>2)||(-best_ofs[used+1]<longlen)) {
              int tmp=cache[used+1]-len_lz(-best_ofs[used+1],best_len[used+1],used+1)
                  +len_lz(-best_ofs[used+1],best_len[used+1]+1,used);
	      if (tmp<=res) {
	        res=tmp;
                my_best_ofs=best_ofs[used+1];
                my_best_len=best_len[used+1]+1;
                my_use_olz=use_olz[used+1];
                my_olz_len=olz_len[used+1];
	      }
		notskip = 0;
//              goto done;
            }
          }
        }
      }
    }
    
    pos=same[used];
    if (pos<0) goto done;
if (notskip) {
      //int tmp=1+len_encode(used-pos-1,used);
      len=samelen[used];
      //max_match=2;//my_best_len+1;//2;
      //if (used-pos>=longlen) { max_match++;}
      int ll=(used-pos>=shortlen)?1:0;
      for(j=2+ll;j<=len;j+=cacherle[used+j]) {
      //for(j=len;j>=max_match;j-=len_encode_l_dec(j-ll)) {
        int tmp2=len_lz(used-pos,j-ll,used);
        tmp2+=cache[used+j];
        if (tmp2<=res) {
          res=tmp2;
          my_best_ofs=pos-used;
          my_best_len=j;
          my_use_olz=0;
          my_olz_len=0;
        }
      }
      max_match=len;

      if (len<left && len>=2+ll) {
        CHECK_OLZ
      }
    }
if (notskip)
      for(;;) {
        int slen=samelen[pos];
        pos=same[pos];
        if (used-pos>=shortlen) break;
        if (pos<0) break;
        if (len>slen) {
          len=slen;
        } else if (len==slen) {
          len+=cmpstr(used+len,pos+len);
        } 
        if (len>max_match) {
          //int tmp=1+len_encode(used-pos-1,used);
          for(j=max_match+1;j<=len;j+=cacherle[used+j]) {
            int tmp2=len_lz(used-pos,j, used);
            tmp2+=cache[used+j];
            if (tmp2<=res) {
              res=tmp2;
              my_best_ofs=pos-used;
              my_best_len=j;
              my_use_olz=0;
              my_olz_len=0;
            }
          }
	  max_match=len;
        }
        if (len<left) {
          CHECK_OLZ
        }
      }
    max_match=my_best_len+1;//2;
int medium = pos2sorted[used];
if (!notskip) goto done;
    int top=sorted_prev[medium];
    int bottom=sorted_next[medium];
    int len_top=(top>=0) ? sorted_len[top]: 0;
    int len_bottom=(bottom >= 0) ? sorted_len[medium]:0;

    int my_min_ofs=used+1;
    while (top>=0 || bottom >=0) {
      match_check_max--;
      if (match_check_max==0) goto done;
      if (len_top>len_bottom) {
        pos=sorted[top];
	len=len_top;
	top=sorted_prev[top];
        len_top = (top>=0) ? min(len_top,sorted_len[top]):0;
      } else {
        pos=sorted[bottom];
	len=len_bottom;
        len_bottom = (bottom >= 0) ? min(len_bottom,sorted_len[bottom]):0;
	bottom=sorted_next[bottom];
      }
      if (used-pos<shortlen) continue; // we already checked it
      if (len<=2) goto done;
      if (my_min_ofs>used-pos) {
      my_min_ofs=used-pos;//we are checking matches in decreasing order. we need to check next matches only if those are shorter
      //int tmp=1+len_encode(used-pos-1,used);
      for(j=3;j<=len;j+=cacherle[used+j]) {
        int tmp2=len_lz(used-pos,j-1,used);
        tmp2+=cache[used+j];
        if (tmp2<=res) {
          res=tmp2;
          my_best_ofs=pos-used;
          my_best_len=j;
          my_use_olz=0;
          my_olz_len=0;
        }
      }}

      if (len<left) {
          CHECK_OLZ
      }
    }    

done:
    if (sorted_prev[medium]>=0) {
      sorted_len[sorted_prev[medium]] = min(sorted_len[sorted_prev[medium]],
                                            sorted_len[medium]);
      sorted_next[sorted_prev[medium]]=sorted_next[medium];
    }
    if (sorted_next[medium]>=0) {
      sorted_prev[sorted_next[medium]]=sorted_prev[medium];
    }

    best_ofs[used]=my_best_ofs;
    best_len[used]=my_best_len;
    use_olz[used]=my_use_olz;
    olz_len[used]=my_olz_len;
    cache[used]=res;
    pos=1;
    while(cache[used+pos]>=res) pos++;
    cacherle[used]=pos;

    if ((i&0xFFF)==0) {
      printf("\x0D%d left ",i);
      fflush(stdout);
    }
  }

  res=8+cache[1];
  printf("\x0Dres=%d\n",res);
  res+=7;
  res>>=3;
  printf("res bytes=%d\n",res);
  if (res>n) {
    return n;
  };

  /* now we can easily generate compressed stream */
  initout();
  int lit;
  for(i=1;i<n;) {
    lit=0;
    while (best_len[i+lit]==1) {
      lit++;
      if (i+lit==n) break;
    }
    if (i+lit==n) {
      put_run(i,lit);
      break;
    }
    if (lit>=MIN_RUN) {
      put_run(i,lit);
      i+=lit;
      lit=0;
    }
    i+=lit;
dolz:
    put_match(i-lit,lit,best_ofs[i],best_len[i]);
    int ofs=best_ofs[i];
    int len=olz_len[i];
    lit=use_olz[i];
    i+=best_len[i];
    if (lit>0) {
      if (lit>MIN_RUN) {
        put_run(i,lit-1);
	i+=lit-1;
	lit=1;
      }
      i+=lit;
      if ((use_olz[i])&&(len==best_len[i])&&(ofs==best_ofs[i])) goto dolz;
      put_match(i-lit,lit,ofs,len);
      i+=len;
    }
  }
  printf("out bytes=%d\n",outpos);

  return outpos;
}

int dorle(signed long n)
{
  long i;
  byte *out=out_buf;
  *out++=in_buf[0];
  for(i=1;i<n;) {
  //printf("rle i=%d to %d\n",i,out-out_buf);
    *out++=in_buf[i];
    if (in_buf[i] != in_buf[i-1])
      i++;
    else {
      byte b=in_buf[i];
      byte cnt=0;
      while(cnt<255&&i<n-1&& b==in_buf[i+1]) {i++;cnt++;}
      *out++=b+cnt;
      i++;
    }
  }
  n=out-out_buf;
  printf("after rle n=%d\n",n);
  memcpy(in_buf,out_buf,n);
  return n;
}

void e8(signed long n) {
  long i;
  long *op;
  for(i=0; i<n-5;) {
    byte b = in_buf[i];
    if ((b == 0xF) && (in_buf[i+1] & 0xF0) == 0x80) {
      i++;
      b = 0xe8;
    }
    b &= 0xFE;
    i++;  
    if (b == 0xe8) {
       op = (long *)(in_buf+i);
       if (*op >= -i && *op < n-i) {
         *op += i;
       } else if ( *op >= n-i && *op < n ) {
         *op -= n; // to [-i,1] 
       }
       i+=4;
    }
  }
}

int main(int argc,char *argv[]) {
  FILE *ifd,*ofd;
  int n,i,res,bres,blz;
  byte b;

  if (argc<3) {
    printf("usage: lzoma input output\n");
    exit(0);
  }
  int arg=1;
  char *inf=argv[arg++];
  char *ouf=argv[arg++];
  ifd=fopen(inf,"rb");
  ofd=fopen(ouf,"wb");
  while((n=fread(in_buf,1,MAX_SIZE,ifd))>0) {
    printf("got %d bytes, packing %s into %s...\n",n,inf,ouf);
    e8(n);
    //n=dorle(n);
    bres=pack(n);
    memcpy(out_best,out_buf,bres);
    if (res==n) {
      fwrite(&n,4,1,ofd);
      fwrite(&n,4,1,ofd);
      fwrite(in_buf,1,n,ofd);
    } else {
      fwrite(&bres,4,1,ofd);
      fwrite(&n,4,1,ofd);
/*      if (breaklz==128) b=6;
      if (breaklz==256) b=7;
      if (breaklz==512) b=8;
      if (breaklz==1024) b=9;
      if (breaklz==2048) b=10;
      if (breaklz==4096) b=11;
      if (breaklz==8192) b=12;
      fwrite(&b,1,1,ofd);*/
      fwrite(out_best,1,bres,ofd);
//  for (i=0;i<n-1;i++) {printf("%d%s\n",cache[i],(cache[i]>=cache[i+1])?"":" !!!");};
    }
  }
  printf("closing files let=%d let7=%d lz=%d olz=%d\n",stlet,stlet7,stlz,stolz);
  close(ifd);
  close(ofd);
  return 0;
}
