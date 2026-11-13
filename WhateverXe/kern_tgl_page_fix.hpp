//
//  kern_tgl_page_fix.hpp
//  WhateverXe
//
//  Exact page fixes for the Tiger Lake 9A49 user-space drivers (16.0.32).
//

#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// VA: AppleIntelTGLGraphicsVADriver lists yuv2 and NV12 as video-processing
// outputs. Clearing the count of that one list makes AppleGVA use its non-VPHAL
// path instead of the media-object stream that stalls RCS on streamed video
// (b00t0x/WhateverXe#81).
// XFER: the same driver lists NV12/BGRA/yuvs... pixel-transfer outputs for its two
// video-processing contexts (0x200 and 0x800) that AppleGVA creates for scaling and
// colour conversion. Creating one dereferences a render component the driver never
// allocates (VA +0x1336f3, NULL+8), crashing WindowServer, QuickTime and
// mediaanalysisd (b00t0x/WhateverXe#23). Clearing both output counts makes AppleGVA
// use its software path, as it does when the driver is absent.
// MTL: -[MTLIGAccelCommandBuffer dealloc] skips deleting the command buffer's
// private POSH buffer when it was the queue's only active one, leaking 76 KB per
// command buffer (b00t0x/WhateverXe#47).
// Pages are matched by file offset, fields and a full SHA-256, once the kernel
// has validated them, so the signed bundles stay untouched.
namespace TGLPageFix {

constexpr size_t PageSize = 4096;
enum Kind { None = -1, VA = 0, MTL = 1, XFER = 2, KindCount = 3 };

constexpr uint64_t VAOffset = 0x786000;
constexpr unsigned VACount = 0xfc4;
constexpr unsigned VAFormat = 0xfc8;
constexpr uint32_t VACountValue = 2;
constexpr uint8_t VADigest[32] {
	0x2a, 0x1a, 0x24, 0x00, 0xbb, 0x67, 0x29, 0xc4, 0xac, 0x13, 0x33, 0x78, 0x64, 0x3d, 0x46, 0x9c,
	0x88, 0xcc, 0xbe, 0x2c, 0x84, 0xe8, 0xc1, 0x6e, 0x13, 0x35, 0x18, 0x16, 0xb6, 0xd0, 0xff, 0x2a
};

constexpr uint64_t XferOffset = 0x787000;
constexpr unsigned XferCount200 = 0x824; // Output count of the 0x200 context list.
constexpr unsigned XferCount800 = 0xc54; // Output count of the 0x800 context list.
constexpr uint32_t XferCountValue = 8;
constexpr uint8_t XferDigest[32] {
	0x12, 0x37, 0x78, 0x72, 0xd5, 0x5b, 0xdc, 0xb5, 0x25, 0xa6, 0xb6, 0xbe, 0x6b, 0x0d, 0xb9, 0xd7,
	0xbf, 0xe2, 0xf5, 0x02, 0xe5, 0x28, 0xf9, 0x0c, 0x57, 0xf4, 0xb9, 0xe9, 0xab, 0xed, 0xc9, 0x04
};

constexpr uint64_t MTLOffset = 0x7e000;
constexpr unsigned MTLJump = 0xd87;
constexpr uint8_t MTLDigest[32] {
	0xaf, 0x8d, 0x71, 0x9b, 0x2d, 0x4f, 0x19, 0x7a, 0xcb, 0x0f, 0x22, 0xab, 0x63, 0x1f, 0x0c, 0xea,
	0xd2, 0xf8, 0x72, 0xfe, 0x3d, 0x0d, 0x13, 0x4c, 0xd5, 0x9b, 0xa6, 0x01, 0x24, 0xce, 0xca, 0xe6
};

inline uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

inline void block(uint32_t state[8], const uint8_t *p) {
	static constexpr uint32_t k[64] {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	};
	uint32_t w[64];
	for (unsigned i = 0; i < 16; i++)
		w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
	for (unsigned i = 16; i < 64; i++) {
		const auto s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
		const auto s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32_t v[8];
	memcpy(v, state, sizeof(v));
	for (unsigned i = 0; i < 64; i++) {
		const auto t1 = v[7] + (rotate(v[4], 6) ^ rotate(v[4], 11) ^ rotate(v[4], 25)) +
			((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
		const auto t2 = (rotate(v[0], 2) ^ rotate(v[0], 13) ^ rotate(v[0], 22)) +
			((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
		v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1;
		v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
	}
	for (unsigned i = 0; i < 8; i++) state[i] += v[i];
}

// SHA-256 of one page. Runs in the page-in path, so it does not allocate.
inline void pageDigest(const uint8_t *page, uint8_t result[32]) {
	uint32_t state[8] {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	for (size_t i = 0; i < PageSize; i += 64) block(state, page + i);
	uint8_t padding[64] {0x80};
	padding[62] = 0x80; // 32768 bits, big-endian.
	block(state, padding);
	for (unsigned i = 0; i < 8; i++)
		for (unsigned j = 0; j < 4; j++) result[4 * i + j] = uint8_t(state[i] >> (24 - 8 * j));
}

inline bool digestIs(const uint8_t *page, const uint8_t expected[32]) {
	uint8_t actual[32];
	pageDigest(page, actual);
	return !memcmp(actual, expected, sizeof(actual));
}

inline Kind classify(const void *data, size_t size, uint64_t offset) {
	if (!data || size != PageSize) return None;
	auto page = static_cast<const uint8_t *>(data);
	if (offset == VAOffset) {
		uint32_t count;
		memcpy(&count, page + VACount, sizeof(count));
		return count == VACountValue && !memcmp(page + VAFormat, "yuv2", 4) && digestIs(page, VADigest) ? VA : None;
	}
	if (offset == XferOffset) {
		uint32_t a, b;
		memcpy(&a, page + XferCount200, sizeof(a));
		memcpy(&b, page + XferCount800, sizeof(b));
		return a == XferCountValue && b == XferCountValue && digestIs(page, XferDigest) ? XFER : None;
	}
	if (offset == MTLOffset)
		return page[MTLJump] == 0x75 && page[MTLJump + 1] == 0x0d && digestIs(page, MTLDigest) ? MTL : None;
	return None;
}

inline void apply(Kind kind, void *data) {
	auto page = static_cast<uint8_t *>(data);
	if (kind == VA) {
		memset(page + VACount, 0, sizeof(uint32_t));
	} else if (kind == XFER) {
		memset(page + XferCount200, 0, sizeof(uint32_t));
		memset(page + XferCount800, 0, sizeof(uint32_t));
	} else if (kind == MTL) {
		page[MTLJump] = 0x90;
		page[MTLJump + 1] = 0x90;
	}
}

} // namespace TGLPageFix
