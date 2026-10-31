//
//  kern_tgl_busy.hpp
//  WhateverXe
//
//  Engine busy time for the native statistics page under host scheduling.
//

#pragma once
#include <stdint.h>

// IGAccelStatistics::writePerformanceStats derives "Device Utilization %" from
// per-unit busy accumulators in the accelerator's statistics page (owner+0xE00,
// unit stride 0x70, busy nanoseconds at +0x190). Graphics firmware fills them;
// with -disablegfxfirmware nothing does, so every utilization reads zero.
// The native host CSB path already measures each context's GPU run time from
// its saved CTX_TIMESTAMP (IGHwCsStatsTracker::updateWithContextUtilizationData,
// on every switch-out). Add that run time to the engine's accumulator.
namespace TGLBusy {

constexpr unsigned Engines = 6;
constexpr uint32_t StatsPage = 0xE00, BusyBase = 0x190, UnitStride = 0x70;
// IGTelemetryManager::getStatisticsGlobalUsage reads unit i's busy time at
// BusyBase + i * 2 * UnitStride, so only units 0, 2 and 4 are ever reported and
// Compute (engine 1) never is. Feed the three slots it reads, all inside the
// 0x430-byte page: Render+Compute, Blit, and the media engines.
// kIGHwCsDesc engine IDs: 0 Render, 1 Compute, 2 Blit, 3 VCS0, 4 VCS2, 5 VECS0.
constexpr unsigned Slots = 3;
constexpr uint8_t SlotOfEngine[Engines] {0, 0, 1, 2, 2, 2};
// IGHwCsStatsTracker: owning accelerator, last context run time (ns) per engine.
constexpr uint32_t TrackerOwner = 0x10, TrackerLastRun = 0x658;
// IGHardwareContext: engine index.
constexpr uint32_t ContextEngine = 0x78;
// A single switch-out cannot account more than this (guards a bogus timestamp).
constexpr uint64_t MaxRunNs = 10ULL * 1000 * 1000 * 1000;

inline uint64_t *accumulator(uint8_t *owner, unsigned engine) {
	if (!owner || engine >= Engines) return nullptr;
	auto page = *reinterpret_cast<uint8_t **>(owner + StatsPage);
	return page ? reinterpret_cast<uint64_t *>(page + BusyBase + SlotOfEngine[engine] * 2 * UnitStride) : nullptr;
}

// After the native tracker ran for `context`, account its last run time.
inline bool account(uint8_t *tracker, uint8_t *context) {
	if (!tracker || !context) return false;
	const unsigned engine = context[ContextEngine];
	if (engine >= Engines) return false;
	const uint64_t run = *reinterpret_cast<uint64_t *>(tracker + TrackerLastRun + engine * 8);
	auto busy = accumulator(*reinterpret_cast<uint8_t **>(tracker + TrackerOwner), engine);
	if (!busy || !run || run > MaxRunNs) return false;
	__atomic_fetch_add(busy, run, __ATOMIC_RELAXED);
	return true;
}

// "Device Utilization %" is busy time scaled by current/maximum frequency. The
// donor derives both from Gen11 registers that do not hold them on Tiger Lake
// (the scaled value exceeded 100). Use GEN12_RPSTAT1 CAGF (16.67 MHz units) and
// RP_STATE_CAP RP0 (50 MHz units). The scaled value never exceeds busy time.
inline uint32_t scaledUtilization(uint32_t busy, uint32_t rpstat1, uint32_t rpStateCap) {
	if (busy > 100) busy = 100;
	const uint32_t cur = ((rpstat1 >> 11) & 0x1FF) * 50 / 3, max = (rpStateCap & 0xFF) * 50;
	if (!cur || !max) return busy;
	return cur >= max ? busy : busy * cur / max;
}

} // namespace TGLBusy
