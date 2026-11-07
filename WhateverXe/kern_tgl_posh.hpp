//
//  kern_tgl_posh.hpp
//  WhateverXe
//
//  Metal driver POSH buffer leak workaround (b00t0x/WhateverXe#47).
//

#pragma once
#include <stddef.h>
#include <stdint.h>

// AppleIntelTGLGraphicsMTLDriver 16.0.32 gives a command buffer a private
// 0x12010-byte POSH buffer whenever its queue has none, but
// -[MTLIGAccelCommandBuffer dealloc] skips deleting it when the buffer was the
// queue's only active one, assuming the queue owns it. The queue's
// m_pPoshUsedAsTarget is only ever NULL, so each such command buffer leaks
// 76 KB, and WindowServer loses about 4.5 MB/s. Platform processes accept the
// bundle only with its original signature, so the file cannot be patched;
// Lilu drops the jne in memory once the matching page passes validation.
namespace TGLPosh {

constexpr const char *DriverPath =
	"/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver";

// mov rax, [m_isOnlyActiveCommandBuffer]; cmp byte [r14+rax], 0; jne skip;
// call operator delete. The RIP-relative displacements pin the 16.0.32 build.
constexpr uint8_t Find[] {
	0x48, 0x8b, 0x05, 0x26, 0xb1, 0x41, 0x00, 0x41, 0x80, 0x3c,
	0x06, 0x00, 0x75, 0x0d, 0xe8, 0x7a, 0xfa, 0x01, 0x00
};
constexpr uint8_t Replace[] {
	0x48, 0x8b, 0x05, 0x26, 0xb1, 0x41, 0x00, 0x41, 0x80, 0x3c,
	0x06, 0x00, 0x90, 0x90, 0xe8, 0x7a, 0xfa, 0x01, 0x00
};
static_assert(sizeof(Find) == sizeof(Replace), "POSH patch sizes differ");

} // namespace TGLPosh
