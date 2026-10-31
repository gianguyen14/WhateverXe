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
// The host CSB consumer marks engines active/idle; a statistics read folds the
// running interval in so a continuously busy engine is not reported idle.
namespace TGLBusy {

constexpr unsigned Engines = 6;
constexpr uint32_t StatsPage = 0xE00, BusyBase = 0x190, UnitStride = 0x70;

struct Clock {
	uint64_t since[Engines] {}; // mach_absolute_time of the last fold; 0 = idle

	// Idle-to-active: start an interval unless one is already open.
	void active(unsigned engine, uint64_t now) {
		if (engine >= Engines || !now) return;
		uint64_t idle = 0;
		__atomic_compare_exchange_n(&since[engine], &idle, now, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	}

	// Active-to-idle: close the open interval into the accumulator.
	void idle(unsigned engine, uint64_t now, uint64_t *busy) {
		if (engine >= Engines) return;
		const uint64_t start = __atomic_exchange_n(&since[engine], 0, __ATOMIC_ACQ_REL);
		if (start && busy && now > start) __atomic_fetch_add(busy, now - start, __ATOMIC_RELAXED);
	}

	// Statistics read: account the running part of every open interval.
	void fold(uint64_t now, uint64_t *const busy[Engines]) {
		for (unsigned e = 0; e < Engines; ++e) {
			uint64_t start = __atomic_load_n(&since[e], __ATOMIC_ACQUIRE);
			if (!start || now <= start) continue;
			if (__atomic_compare_exchange_n(&since[e], &start, now, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) && busy[e])
				__atomic_fetch_add(busy[e], now - start, __ATOMIC_RELAXED);
		}
	}

	// Engine reset or wake: drop the open interval without accounting it.
	void drop(unsigned engine) {
		if (engine < Engines) __atomic_store_n(&since[engine], 0, __ATOMIC_RELEASE);
	}
};

inline uint64_t *accumulator(uint8_t *owner, unsigned engine) {
	if (!owner || engine >= Engines) return nullptr;
	auto page = *reinterpret_cast<uint8_t **>(owner + StatsPage);
	return page ? reinterpret_cast<uint64_t *>(page + BusyBase + engine * UnitStride) : nullptr;
}

} // namespace TGLBusy
