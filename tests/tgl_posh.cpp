// CPU-only tests: clang++ -std=c++17 -fsanitize=address,undefined tests/tgl_posh.cpp
#include "../WhateverXe/kern_tgl_posh.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <mach-o/loader.h>

using namespace TGLPosh;

int main() {
	// The patch only drops the jne that skips operator delete.
	size_t changed = 0;
	for (size_t i = 0; i < sizeof(Find); i++) {
		if (Find[i] == Replace[i]) continue;
		assert((i == 12 || i == 13) && Replace[i] == 0x90);
		changed++;
	}
	assert(changed == 2 && Find[12] == 0x75 && Find[13] == 0x0d);

	// Lilu patches every match it finds, so with the installed driver the
	// pattern must occur once in __TEXT,__text and within one page.
	std::ifstream file(DriverPath, std::ios::binary);
	if (!file) {
		puts("tgl_posh ok (driver not installed)");
		return 0;
	}
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	assert(data.size() > sizeof(mach_header_64));
	auto header = reinterpret_cast<const mach_header_64 *>(data.data());
	assert(header->magic == MH_MAGIC_64 && header->cputype == CPU_TYPE_X86_64);
	const section_64 *text = nullptr;
	auto command = reinterpret_cast<const load_command *>(header + 1);
	for (uint32_t i = 0; i < header->ncmds && !text; i++) {
		if (command->cmd == LC_SEGMENT_64) {
			auto segment = reinterpret_cast<const segment_command_64 *>(command);
			auto sections = reinterpret_cast<const section_64 *>(segment + 1);
			for (uint32_t s = 0; s < segment->nsects; s++)
				if (!strcmp(segment->segname, "__TEXT") && !strncmp(sections[s].sectname, "__text", 16)) text = &sections[s];
		}
		command = reinterpret_cast<const load_command *>(reinterpret_cast<const uint8_t *>(command) + command->cmdsize);
	}
	assert(text && text->offset + text->size <= data.size());
	size_t matches = 0, at = 0;
	for (size_t i = text->offset; i + sizeof(Find) <= text->offset + text->size; i++) {
		if (!memcmp(&data[i], Find, sizeof(Find))) {
			matches++;
			at = i;
		}
	}
	if (!matches) {
		puts("tgl_posh ok (different driver build, patch inert)");
		return 0;
	}
	assert(matches == 1);
	const uint64_t vm = text->addr + (at - text->offset);
	assert(vm / 4096 == (vm + sizeof(Find) - 1) / 4096);
	printf("tgl_posh ok (16.0.32 match at 0x%zx)\n", at);
	return 0;
}
