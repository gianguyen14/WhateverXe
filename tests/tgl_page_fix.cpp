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
	puts("tgl_page_fix ok");
	return 0;
}
