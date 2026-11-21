#include "../WhateverXe/kern_tgl_page_fix.hpp"
#include <cassert>
#include <cstdio>
int main(int argc,char **argv) {
 assert(argc==3); unsigned char code[308],disk[308];
 auto f=fopen(argv[1],"rb");assert(f);assert(fread(code,1,308,f)==308);fclose(f);
 f=fopen(argv[2],"rb");assert(f);assert(fread(disk,1,308,f)==308);fclose(f);
 assert(TGLPageFix::validatorQualified(code,308));
 assert(!TGLPageFix::validatorQualified(disk,308));
 for(unsigned i=0;i<308;++i)for(unsigned b=0;b<8;++b) {
  code[i]^=1U<<b;assert(!TGLPageFix::validatorQualified(code,308));code[i]^=1U<<b;
 }
 assert(!TGLPageFix::validatorQualified(code,307));
 assert(!TGLPageFix::validatorQualified(nullptr,308));
 puts("KC fingerprint: exact match; disk and all 2464 single-bit mutations rejected");
}
