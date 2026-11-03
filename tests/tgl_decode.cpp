// CPU-only tests: clang++ -std=c++17 -fsanitize=address,undefined tests/tgl_decode.cpp
#include "../WhateverXe/kern_tgl_decode.hpp"
#include <cassert>
#include <cstdio>

using namespace TGLDecode;

int main() {
	// Full and kernel-truncated decoder service names.
	assert(refused("VTDecoderXPCService"));
	assert(refused("VTDecoderXPCServ"));
	// The encoder service and other clients keep hardware video contexts.
	assert(!refused("VTEncoderXPCService"));
	assert(!refused("VTEncoderXPCServ"));
	assert(!refused("WindowServer"));
	assert(!refused("com.apple.WebKit.GPU"));
	assert(!refused("VTDecoder"));
	assert(!refused(""));
	assert(!refused(nullptr));
	puts("tgl_decode ok");
	return 0;
}
