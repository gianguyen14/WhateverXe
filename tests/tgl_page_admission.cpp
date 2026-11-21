#include "../WhateverXe/kern_tgl_page_fix.hpp"
#include <cassert>
int main() {
 using namespace TGLPageFix;
 Admission a;
 assert(a.status==AdmissionStatus::Entered);
 assert(admissionGate(false,true,true,true,true,true)==AdmissionStatus::NotRequested);
 assert(admissionGate(true,false,true,true,true,true)==AdmissionStatus::NoGPU);
 assert(admissionGate(true,true,false,true,true,true)==AdmissionStatus::UnsupportedKernel);
 assert(admissionGate(true,true,true,false,true,true)==AdmissionStatus::ProfileRejected);
 assert(admissionGate(true,true,true,true,false,true)==AdmissionStatus::VersionRejected);
 assert(admissionGate(true,true,true,true,true,false)==AdmissionStatus::PageSizeRejected);
 assert(admissionGate(true,true,true,true,true,true)==AdmissionStatus::Qualified);
 unsigned char code[308] {}, digest[32] {}, page[4096] {}, reference[32];
 assert(validatorFingerprint(code,308,digest));
 pageDigest(page,reference);assert(!memcmp(digest,reference,32));
 assert(!validatorFingerprint(nullptr,308,digest));
 assert(!validatorFingerprint(code,307,digest));
 assert(!validatorQualified(code,308));
}
