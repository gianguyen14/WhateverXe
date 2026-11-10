// CPU-only tests: clang++ -std=c++17 -fsanitize=address,undefined tests/tgl_page_fix.cpp
#include "../WhateverXe/kern_tgl_page_fix.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace TGLPageFix;

static const char *VAPath = "/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsVADriver";
static const char *MTLPath = "/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver";

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
	puts("tgl_page_fix ok");
	return 0;
}
