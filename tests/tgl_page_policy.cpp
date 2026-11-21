#include "../WhateverXe/kern_tgl_page_fix.hpp"
#include <cassert>
#include <cstdio>
int main(int argc,char **argv) {
 if(argc==3) {
  unsigned char code[308];auto f=fopen(argv[1],"rb");assert(f);assert(fread(code,1,sizeof(code),f)==sizeof(code));fclose(f);
  assert(TGLPageFix::validatorQualified(code,sizeof(code)));code[0]^=1;assert(!TGLPageFix::validatorQualified(code,sizeof(code)));
  unsigned char page[4096];f=fopen(argv[2],"rb");assert(f);assert(!fseek(f,TGLPageFix::XferOffset,SEEK_SET));assert(fread(page,1,sizeof(page),f)==sizeof(page));fclose(f);
  assert(TGLPageFix::classify(page,sizeof(page),TGLPageFix::XferOffset)==TGLPageFix::XFER);
  page[0]^=1;assert(TGLPageFix::classify(page,sizeof(page),TGLPageFix::XferOffset)==TGLPageFix::None);page[0]^=1;
  assert(TGLPageFix::classify(page,4095,TGLPageFix::XferOffset)==TGLPageFix::None);
  TGLPageFix::apply(TGLPageFix::XFER,page);assert(TGLPageFix::classify(page,sizeof(page),TGLPageFix::XferOffset)==TGLPageFix::None);
 }

 using namespace TGLPageFix;
 // This kernel reports all four logical validation bits even on 4K x86.
 assert(validationSpan(25,0xf,0,0,4096)==4096);
 assert(validationSpan(25,1,0,0,4096)==0);
 assert(validationSpan(25,0xf,1,0,4096)==0);
 assert(validationSpan(25,0xf,0,1,4096)==0);
 assert(validationSpan(25,0xf,0,0,16384)==0);
 assert(validationSpan(26,0xf,0,0,4096)==0);
 assert(policyAllows(25,true,false,XFER));
 for(int k=0;k<KindCount;++k) if(k!=XFER) assert(!policyAllows(25,true,false,static_cast<Kind>(k)));
 assert(!policyAllows(25,false,false,XFER));
 assert(!policyAllows(25,true,true,XFER));
 for(int k=0;k<KindCount;++k) assert(policyAllows(24,false,false,static_cast<Kind>(k)));
 assert(!policyAllows(26,true,false,XFER));
 unsigned char wrong[308] {}; assert(!validatorQualified(wrong,sizeof(wrong)));
 puts("tgl_page_policy ok");
}
