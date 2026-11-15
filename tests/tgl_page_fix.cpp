// CPU-only tests: clang++ -std=c++17 -fsanitize=address,undefined tests/tgl_page_fix.cpp
#include "../WhateverXe/kern_tgl_page_fix.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace TGLPageFix;

// Reading an installed driver returns the page cache, which the running kext has
// already patched, so on a machine with the fixes enabled point WEX_VA_DRIVER and
// WEX_MTL_DRIVER at the driver files of the Graphics package instead.
static const char *VAPath = getenv("WEX_VA_DRIVER") ? getenv("WEX_VA_DRIVER") : "/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsVADriver";
// The dyld shared cache sub-file that holds AppleGVACore. As with the drivers, reading the
// installed file on a machine with -wexgvanoassert may return the patched page.
static const char *CachePath = getenv("WEX_DYLD_CACHE_02") ? getenv("WEX_DYLD_CACHE_02") : "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_x86_64h.02";
static const char *MTLPath = getenv("WEX_MTL_DRIVER") ? getenv("WEX_MTL_DRIVER") : "/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver";

static bool readPage(const char *path, uint64_t offset, std::vector<uint8_t> &page) {
	std::ifstream file(path, std::ios::binary);
	if (!file) return false;
	page.resize(PageSize);
	file.seekg(offset);
	file.read(reinterpret_cast<char *>(page.data()), PageSize);
	return size_t(file.gcount()) == PageSize;
}

static void checkDriver(const char *path, uint64_t offset, Kind kind, unsigned changedAt, unsigned changedBytes) {
	std::vector<uint8_t> page;
	if (!readPage(path, offset, page)) {
		puts("driver not installed");
		return;
	}
	if (classify(page.data(), PageSize, offset) != kind) {
		puts("different driver build, fix inert");
		return;
	}
	assert(classify(page.data(), PageSize, offset + PageSize) == None);
	assert(classify(page.data(), PageSize - 1, offset) == None);
	auto original = page;
	apply(kind, page.data());
	size_t changed = 0;
	for (size_t i = 0; i < PageSize; i++) {
		if (page[i] == original[i]) continue;
		assert(i >= changedAt && i < changedAt + changedBytes);
		changed++;
	}
	assert(changed == changedBytes - (kind == VA ? 3 : 0)); // The VA count only has one non-zero byte.
	assert(classify(page.data(), PageSize, offset) == None); // A patched page never matches again.
	auto tampered = original;
	tampered[7] ^= 1;
	assert(classify(tampered.data(), PageSize, offset) == None);
}

// Two separate counts, so the generic one-range check does not fit.
static void checkXfer() {
	std::vector<uint8_t> page;
	if (!readPage(VAPath, XferOffset, page)) return;
	if (classify(page.data(), PageSize, XferOffset) != XFER) {
		puts("different driver build, xfer fix inert");
		return;
	}
	assert(classify(page.data(), PageSize, XferOffset + PageSize) == None);
	auto original = page;
	apply(XFER, page.data());
	size_t changed = 0;
	for (size_t i = 0; i < PageSize; i++) {
		if (page[i] == original[i]) continue;
		assert(i == XferCount200 || i == XferCount800);
		changed++;
	}
	assert(changed == 2); // Each count has one non-zero byte.
	assert(classify(page.data(), PageSize, XferOffset) == None);
	auto tampered = original;
	tampered[7] ^= 1;
	assert(classify(tampered.data(), PageSize, XferOffset) == None);
}

// The three windows are matched by exact bytes at a pinned offset, so a synthetic
// page is enough, and the installed driver is checked on top of it.
static void checkNullGuard() {
	for (unsigned i = 0; i < 3; i++) {
		const auto &w = NullGuardWindows[i];
		const auto kind = NullGuardKinds[i];
		assert(w.at + w.length <= PageSize);
		assert(memcmp(w.from, w.to, w.length) != 0);
		std::vector<uint8_t> page(PageSize, 0xcc);
		memcpy(page.data() + w.at, w.from, w.length);
		assert(classify(page.data(), PageSize, w.pageOffset) == kind);
		assert(classify(page.data(), PageSize, w.pageOffset + PageSize) == None);
		assert(classify(page.data(), PageSize - 1, w.pageOffset) == None);
		auto original = page;
		apply(kind, page.data());
		for (size_t j = 0; j < PageSize; j++) {
			const bool inside = j >= w.at && j < w.at + w.length;
			assert(inside ? page[j] == w.to[j - w.at] : page[j] == original[j]);
		}
		assert(classify(page.data(), PageSize, w.pageOffset) == None); // A patched page never matches again.
		for (unsigned b : {0u, w.length / 2, w.length - 1}) { // One changed byte anywhere in the window.
			auto tampered = original;
			tampered[w.at + b] ^= 1;
			assert(classify(tampered.data(), PageSize, w.pageOffset) == None);
		}
		std::vector<uint8_t> blank(PageSize);
		assert(classify(blank.data(), PageSize, w.pageOffset) == None);
		std::vector<uint8_t> driver;
		if (!readPage(VAPath, w.pageOffset, driver)) continue;
		if (classify(driver.data(), PageSize, w.pageOffset) != kind) {
			puts("different driver build, null guard inert");
			continue;
		}
		auto before = driver;
		apply(kind, driver.data());
		for (size_t j = 0; j < PageSize; j++)
			if (driver[j] != before[j]) assert(j >= w.at && j < w.at + w.length);
	}
	// The windows never overlap another fix's page.
	assert(NullGuardWindows[0].pageOffset != XferOffset && NullGuardWindows[0].pageOffset != VAOffset);
	// The anchors lie on distinct pages: a page is classified by its offset alone.
	assert(NullGuardWindows[0].pageOffset != NullGuardWindows[1].pageOffset &&
	       NullGuardWindows[1].pageOffset != NullGuardWindows[2].pageOffset);
}

// One byte of AppleGVACore: Flow_PP0's +0x349 flag is stored as 0 instead of 1.
static void checkGVAAssert() {
	const auto &w = GVAAssertWindow;
	assert(w.at + w.length <= PageSize);
	assert(w.length == sizeof(GVAAssertFrom) && w.length == sizeof(GVAAssertTo));
	size_t differing = 0, at = 0;
	for (size_t i = 0; i < w.length; i++)
		if (w.from[i] != w.to[i]) { differing++; at = i; }
	assert(differing == 1 && w.from[at] == 1 && w.to[at] == 0); // Exactly the immediate 1 -> 0.
	assert(w.from[at - 7] == 0x41 && w.from[at - 6] == 0xc6 && w.from[at - 5] == 0x87); // movb $imm8, disp32(%r15)
	assert(w.from[at - 4] == 0x49 && w.from[at - 3] == 0x03 && w.from[at - 2] == 0 && w.from[at - 1] == 0); // +0x349
	std::vector<uint8_t> page(PageSize, 0xcc);
	memcpy(page.data() + w.at, w.from, w.length);
	assert(classify(page.data(), PageSize, w.pageOffset) == GVAAssert);
	assert(classify(page.data(), PageSize, w.pageOffset + PageSize) == None);
	assert(classify(page.data(), PageSize, w.pageOffset - PageSize) == None);
	assert(classify(page.data(), PageSize - 1, w.pageOffset) == None);
	auto original = page;
	apply(GVAAssert, page.data());
	size_t changed = 0;
	for (size_t j = 0; j < PageSize; j++) {
		if (page[j] == original[j]) continue;
		assert(j == w.at + at);
		changed++;
	}
	assert(changed == 1);
	assert(classify(page.data(), PageSize, w.pageOffset) == None); // A patched page never matches again.
	for (unsigned b : {0u, w.length / 2, w.length - 1}) {
		auto tampered = original;
		tampered[w.at + b] ^= 1;
		assert(classify(tampered.data(), PageSize, w.pageOffset) == None);
	}
	std::vector<uint8_t> blank(PageSize);
	assert(classify(blank.data(), PageSize, w.pageOffset) == None);
	// Distinct from every other fix's page.
	assert(w.pageOffset != VAOffset && w.pageOffset != XferOffset && w.pageOffset != MTLOffset);
	for (const auto &g : NullGuardWindows) assert(w.pageOffset != g.pageOffset);
	std::vector<uint8_t> cache;
	if (!readPage(CachePath, w.pageOffset, cache)) {
		puts("dyld shared cache not readable, gva assert fix not checked on it");
		return;
	}
	const auto kind = classify(cache.data(), PageSize, w.pageOffset);
	if (kind != GVAAssert) {
		puts("different dyld cache build (or already patched), gva assert fix inert");
		return;
	}
	auto before = cache;
	apply(GVAAssert, cache.data());
	for (size_t j = 0; j < PageSize; j++)
		if (cache[j] != before[j]) assert(j == w.at + at);
}

// The two `jne` instructions in AVFPixelProcessCore_PrepareToTransferImages that jumped to
// assert(0) now jump to the function's own error return.
// The VFE windows: exact bytes of the VA driver (0.4.0 and 0.4.4 share these offsets), applied only to their own page.
static void checkVFE() {
	struct Case { Kind kind; const Window *w; } cases[] {{VFEL3, &VFEL3Window}, {VFESites, &VFESitesWindow}, {VFETemplate, &VFETemplateWindow}, {CMFCVersion, &CMFCWindow}, {VFEThreads, &VFEThreadsWindow}};
	for (const auto &c : cases) {
		const auto &w = *c.w;
		assert(w.at + w.length <= PageSize);
		std::vector<uint8_t> page(PageSize, 0xcc);
		memcpy(page.data() + w.at, w.from, w.length);
		assert(classify(page.data(), PageSize, w.pageOffset) == c.kind);
		assert(classify(page.data(), PageSize, w.pageOffset + PageSize) == None);
		assert(classify(page.data(), PageSize - 1, w.pageOffset) == None);
		auto original = page;
		apply(c.kind, page.data());
		size_t changed = 0;
		for (size_t j = 0; j < PageSize; j++) {
			if (page[j] == original[j]) continue;
			assert(j >= w.at && j < w.at + w.length);
			changed++;
		}
		assert(changed > 0 && !memcmp(page.data() + w.at, w.to, w.length));
		assert(classify(page.data(), PageSize, w.pageOffset) == None); // Patched pages are not matched again.
		// The real driver, when the file is available: the window must match the file bytes.
		std::vector<uint8_t> real;
		if (readPage(VAPath, w.pageOffset, real) && !memcmp(real.data() + w.at, w.from, w.length))
			assert(classify(real.data(), PageSize, w.pageOffset) == c.kind);
	}
	// L3: data 0xD0000020 into register 0xB134; the rest of the qword is the same movabs.
	assert(!memcmp(VFEL3To + 10, "\x48\xb8\x34\xb1\x00\x00\x20\x00\x00\xd0", 10));
	// Scoreboard: DW6-DW8 of the template are zero, the stores are nops or a zeroing and.
	for (size_t i = 24; i < 36; i++) assert(VFETemplateTo[i] == 0);
	assert(VFESitesTo[7] == 0x83 && VFESitesTo[8] == 0x67 && VFESitesTo[9] == 0xf8 && VFESitesTo[10] == 0x00); // andl $0, -8(%rdi)
	assert(!memcmp(VFESitesTo + 13, "\x0f\x1f\x40\x00", 4) && !memcmp(VFESitesTo + 19, "\x0f\x1f\x40\x00", 4));
	// Distinct pages of the VA driver, none of them the VA/XFER/MTL pages.
	assert(VFEL3Window.pageOffset != VFESitesWindow.pageOffset && VFESitesWindow.pageOffset != VFETemplateWindow.pageOffset);
	assert(VFEL3Window.pageOffset != VAOffset && VFETemplateWindow.pageOffset != XferOffset);
}

static void checkGVAConfigure() {
	const auto &w = GVAConfigureWindow;
	assert(w.at + w.length <= PageSize && w.length == sizeof(GVAConfigureFrom) && w.length == sizeof(GVAConfigureTo));
	std::vector<size_t> diff;
	for (size_t i = 0; i < w.length; i++)
		if (w.from[i] != w.to[i]) diff.push_back(i);
	assert(diff.size() == 3);
	// jne rel32 at +10 (0f 85): the displacement shrinks, the opcode does not change.
	assert(w.from[10] == 0x0f && w.from[11] == 0x85 && w.to[10] == 0x0f && w.to[11] == 0x85);
	assert(diff[0] == 12 && diff[1] == 13);
	// jne rel8 (75) at +241: only its displacement changes, backwards.
	assert(w.from[241] == 0x75 && w.to[241] == 0x75 && diff[2] == 242 && (int8_t)w.to[242] < 0);
	// Both new targets land on the same instruction: movl $0xffffd43c, %r14d (41 be 3c d4 ff ff).
	const int64_t t1 = int64_t(w.at) + 10 + 6 + int32_t(uint32_t(w.to[12]) | uint32_t(w.to[13]) << 8 | uint32_t(w.to[14]) << 16 | uint32_t(w.to[15]) << 24);
	const int64_t t2 = int64_t(w.at) + 241 + 2 + int8_t(w.to[242]);
	assert(t1 == t2);
	assert(t1 >= int64_t(w.at) && t1 + 6 <= int64_t(w.at + w.length));
	const auto *target = w.from + (t1 - w.at);
	assert(target[0] == 0x41 && target[1] == 0xbe && target[2] == 0x3c && target[3] == 0xd4 && target[4] == 0xff && target[5] == 0xff);
	std::vector<uint8_t> page(PageSize, 0xcc);
	memcpy(page.data() + w.at, w.from, w.length);
	assert(classify(page.data(), PageSize, w.pageOffset) == GVAConfigure);
	assert(classify(page.data(), PageSize, w.pageOffset + PageSize) == None);
	assert(classify(page.data(), PageSize - 1, w.pageOffset) == None);
	auto original = page;
	apply(GVAConfigure, page.data());
	size_t changed = 0;
	for (size_t j = 0; j < PageSize; j++) {
		if (page[j] == original[j]) continue;
		assert(j == w.at + diff[0] || j == w.at + diff[1] || j == w.at + diff[2]);
		changed++;
	}
	assert(changed == 3);
	assert(classify(page.data(), PageSize, w.pageOffset) == None);
	for (unsigned b : {0u, w.length / 2, w.length - 1}) {
		auto tampered = original;
		tampered[w.at + b] ^= 1;
		assert(classify(tampered.data(), PageSize, w.pageOffset) == None);
	}
	assert(w.pageOffset != GVAAssertWindow.pageOffset && w.pageOffset != VAOffset && w.pageOffset != XferOffset && w.pageOffset != MTLOffset);
	for (const auto &g : NullGuardWindows) assert(w.pageOffset != g.pageOffset);
	std::vector<uint8_t> cache;
	if (!readPage(CachePath, w.pageOffset, cache)) return;
	if (classify(cache.data(), PageSize, w.pageOffset) != GVAConfigure) {
		puts("different dyld cache build (or already patched), gva configure fix inert");
		return;
	}
	auto before = cache;
	apply(GVAConfigure, cache.data());
	for (size_t j = 0; j < PageSize; j++)
		if (cache[j] != before[j]) assert(j == w.at + diff[0] || j == w.at + diff[1] || j == w.at + diff[2]);
}

int main() {
	// SHA-256 of a zero page.
	static const uint8_t zero[32] {0xad, 0x7f, 0xac, 0xb2, 0x58, 0x6f, 0xc6, 0xe9, 0x66, 0xc0, 0x04, 0xd7, 0xd1, 0xd1, 0x6b, 0x02,
		0x4f, 0x58, 0x05, 0xff, 0x7c, 0xb4, 0x7c, 0x7a, 0x85, 0xda, 0xbd, 0x8b, 0x48, 0x89, 0x2c, 0xa7};
	std::vector<uint8_t> blank(PageSize);
	assert(digestIs(blank.data(), zero));
	assert(classify(nullptr, PageSize, VAOffset) == None);
	assert(classify(blank.data(), PageSize, VAOffset) == None);
	assert(classify(blank.data(), PageSize, MTLOffset) == None);
	assert(classify(blank.data(), PageSize, 0) == None);
	checkDriver(VAPath, VAOffset, VA, VACount, 4);
	checkDriver(MTLPath, MTLOffset, MTL, MTLJump, 2);
	checkXfer();
	checkNullGuard();
	checkGVAAssert();
	checkGVAConfigure();
	checkVFE();
	{ // -wexnullskip: one byte of the guarded render step window, only when the flag is set.
		const auto &w = NullGuardWindows[2];
		assert(w.to[56] == 0x05 && w.to[55] == 0xe4 && w.to[54] == 0x45 && w.to[53] == 0xc7); // movl $5, -0x1c(%rbp)
		std::vector<uint8_t> page(PageSize, 0xcc);
		memcpy(page.data() + w.at, w.from, w.length);
		NullGuardSkip = true;
		apply(NullGuardRender, page.data());
		NullGuardSkip = false;
		assert(page[w.at + 56] == 0 && !memcmp(page.data() + w.at, w.to, 56) && !memcmp(page.data() + w.at + 57, w.to + 57, w.length - 57));
	}
	puts("tgl_page_fix ok");
	return 0;
}
