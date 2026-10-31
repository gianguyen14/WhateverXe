// CPU-only tests: clang++ -std=c++17 -fsanitize=address,undefined tests/tgl_busy.cpp
#include "../WhateverXe/kern_tgl_busy.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace TGLBusy;

int main() {
	// Layout matches the donor statistics page read by writePerformanceStats.
	alignas(8) uint8_t page[0x1000] {};
	alignas(8) uint8_t owner[0x1000] {};
	uint8_t *p = page;
	memcpy(owner + StatsPage, &p, sizeof(p));
	assert(accumulator(owner, 0) == reinterpret_cast<uint64_t *>(page + 0x190));
	assert(accumulator(owner, 2) == reinterpret_cast<uint64_t *>(page + 0x190 + 2 * 0x70));
	assert(!accumulator(owner, Engines) && !accumulator(nullptr, 0));
	uint8_t *none = nullptr;
	alignas(8) uint8_t empty[0x1000] {};
	memcpy(empty + StatsPage, &none, sizeof(none));
	assert(!accumulator(empty, 0));

	Clock c;
	uint64_t busy[Engines] {};
	uint64_t *all[Engines];
	for (unsigned e = 0; e < Engines; ++e) all[e] = &busy[e];

	// A closed interval is accounted once.
	c.active(0, 100);
	c.active(0, 150); // already active: keeps the original start
	c.idle(0, 400, &busy[0]);
	assert(busy[0] == 300);
	c.idle(0, 500, &busy[0]); // idle without an open interval adds nothing
	assert(busy[0] == 300);

	// A continuously busy engine is visible at every statistics read.
	c.active(2, 1000);
	c.fold(1600, all);
	assert(busy[2] == 600);
	c.fold(2000, all);
	assert(busy[2] == 1000);
	c.idle(2, 2500, &busy[2]);
	assert(busy[2] == 1500);
	c.fold(3000, all); // idle engines stay unchanged
	assert(busy[2] == 1500 && busy[0] == 300);

	// Reset drops the open interval; a later idle cannot account it.
	c.active(4, 10);
	c.drop(4);
	c.idle(4, 90, &busy[4]);
	assert(busy[4] == 0);

	// Out-of-range engines and a zero timestamp are ignored.
	c.active(Engines, 5);
	c.idle(Engines, 9, &busy[0]);
	c.drop(Engines);
	c.active(1, 0);
	c.fold(50, all);
	assert(busy[1] == 0);

	// Missing statistics page: intervals still close without a write.
	c.active(5, 10);
	uint64_t *missing[Engines] {};
	c.fold(20, missing);
	c.idle(5, 30, nullptr);
	assert(busy[5] == 0 && c.since[5] == 0);

	puts("tgl_busy ok");
	return 0;
}
