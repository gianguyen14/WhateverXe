// CPU-only tests: clang++ -std=c++17 -fsanitize=address,undefined tests/tgl_busy.cpp
#include "../WhateverXe/kern_tgl_busy.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace TGLBusy;

int main() {
	// Layout matches the donor statistics page read by writePerformanceStats.
	alignas(8) uint8_t page[0x1000] {};
	alignas(8) uint8_t owner[0x2000] {};
	uint8_t *p = page;
	memcpy(owner + StatsPage, &p, sizeof(p));
	// Slots sit where getStatisticsGlobalUsage reads units 0..2 (stride 0xE0).
	assert(accumulator(owner, 0) == reinterpret_cast<uint64_t *>(page + 0x190));
	assert(accumulator(owner, 1) == accumulator(owner, 0)); // Compute joins Render
	assert(accumulator(owner, 2) == reinterpret_cast<uint64_t *>(page + 0x270));
	for (unsigned e = 3; e < Engines; ++e)
		assert(accumulator(owner, e) == reinterpret_cast<uint64_t *>(page + 0x350));
	assert(0x350 + 8 <= 0x430); // inside the native allocation
	assert(!accumulator(owner, Engines) && !accumulator(nullptr, 0));

	alignas(8) uint8_t tracker[0x800] {};
	alignas(8) uint8_t context[0x100] {};
	uint8_t *o = owner;
	memcpy(tracker + TrackerOwner, &o, sizeof(o));
	auto lastRun = [&](unsigned e, uint64_t ns) { memcpy(tracker + TrackerLastRun + e * 8, &ns, 8); };
	auto busy = [&](unsigned e) { uint64_t v; memcpy(&v, page + 0x190 + SlotOfEngine[e] * 0xE0, 8); return v; };

	// Each switch-out adds that context's run time to its engine.
	context[ContextEngine] = 0;
	lastRun(0, 1500);
	assert(account(tracker, context) && busy(0) == 1500);
	lastRun(0, 2500);
	assert(account(tracker, context) && busy(0) == 4000);
	context[ContextEngine] = 1; // Compute adds to the Render slot
	lastRun(1, 1000);
	assert(account(tracker, context) && busy(0) == 5000);
	context[ContextEngine] = 2;
	lastRun(2, 700);
	assert(account(tracker, context) && busy(2) == 700 && busy(0) == 5000);

	// Zero, oversized and out-of-range samples are ignored.
	lastRun(2, 0);
	assert(!account(tracker, context));
	lastRun(2, MaxRunNs + 1);
	assert(!account(tracker, context) && busy(2) == 700);
	context[ContextEngine] = Engines;
	assert(!account(tracker, context));
	assert(!account(nullptr, context) && !account(tracker, nullptr));

	// Missing statistics page: nothing is written.
	uint8_t *none = nullptr;
	memcpy(owner + StatsPage, &none, sizeof(none));
	context[ContextEngine] = 0;
	lastRun(0, 100);
	assert(!account(tracker, context) && busy(0) == 5000);

	// Frequency scaling: 1100 of 1300 MHz, clamped to busy, unknown -> busy.
	const uint32_t cagf1100 = 66u << 11, cap = 0x0002001A;
	assert(scaledUtilization(56, cagf1100, cap) == 47);
	assert(scaledUtilization(56, 78u << 11, cap) == 56);
	assert(scaledUtilization(56, 90u << 11, cap) == 56);
	assert(scaledUtilization(150, cagf1100, cap) == 84);
	assert(scaledUtilization(40, 0, cap) == 40 && scaledUtilization(40, cagf1100, 0) == 40);
	assert(scaledUtilization(0, cagf1100, cap) == 0);

	// Live sampling: progress within wall time is accounted; wrap is handled.
	assert(liveTicks(1500, 1000, 1000) == 500);
	assert(liveTicks(10, 0xFFFFFFF0u, 1000) == 26);
	assert(liveTicks(1000, 1000, 1000) == 0);          // no progress
	assert(liveTicks(5000, 1000, 1000) == 1000);       // clamped to wall time
	assert(liveTicks(900, 1000, 1000) == 0);           // went backwards
	assert(liveTicks(5000, 1000, 0) == 0);             // first sample: no window
	// 19.2 MHz timestamp: denominator 192, one tick = 52.08 ns.
	assert(ticksToNs(192, 192) == 10000 && ticksToNs(1, 0) == 0);
	assert(nsToTicks(10000, 192) == 192);

	// Slot growth between reads never exceeds the elapsed time.
	assert(clampGrowth(1500, 1000, 1000) == 1500);
	assert(clampGrowth(5000, 1000, 1000) == 2000);
	assert(clampGrowth(1000, 1000, 1000) == 1000);
	assert(clampGrowth(900, 1000, 1000) == 900);

	puts("tgl_busy ok");
	return 0;
}
