//
//  kern_tgl_rps.hpp
//  WhateverXe
//
//  GPU frequency selection under host scheduling.
//

#pragma once
#include <stdint.h>

// With -disablegfxfirmware there is no GuC SLPC, and the donor's request and
// boost inputs to pmNotify stay at RP0, so the GT ran at its maximum clock
// whenever it was awake. On a 15 W part that power comes out of the CPU's
// budget. Choose the request from RC6 residency instead: GEN6_GT_GFX_RC6
// (always-on, 1.28 us units) tells how long the GT slept since the previous
// evaluation. Go up at once under load, come down one step per window.
namespace TGLRPS {

constexpr uint64_t WindowNs = 50ULL * 1000 * 1000;   // evaluation window
constexpr uint64_t RC6UnitNs = 1280;
constexpr uint32_t FullAtActivePct = 80;              // active% that asks for RP0
constexpr uint32_t StepDown = 2;                      // 50 MHz units per window

// RP_STATE_CAP: RP0 [7:0], RP1 [15:8], RPn [23:16], 50 MHz units.
struct Caps { uint32_t rp0, rp1, rpn; };
inline Caps decodeCaps(uint32_t cap) { return {cap & 0xFF, (cap >> 8) & 0xFF, (cap >> 16) & 0xFF}; }

struct Governor {
	uint64_t lastNs {}, windowRc6Ns {};
	uint32_t lastRc6 {}, target {}, activePct {100};

	// Returns the frequency to request (50 MHz units) within [floor, rp0].
	uint32_t step(uint64_t nowNs, uint32_t rc6, const Caps &caps) {
		const uint32_t hi = caps.rp0;
		uint32_t floor = caps.rp1 ? caps.rp1 : caps.rpn;
		if (floor > hi) floor = hi;
		if (!target || target > hi) target = hi;
		if (target < floor) target = floor;
		if (!lastNs || nowNs < lastNs) {
			lastNs = nowNs; lastRc6 = rc6; windowRc6Ns = 0;
			return target;
		}
		windowRc6Ns += static_cast<uint64_t>(rc6 - lastRc6) * RC6UnitNs;
		lastRc6 = rc6;
		const uint64_t elapsed = nowNs - lastNs;
		if (elapsed < WindowNs) return target;
		const uint64_t slept = windowRc6Ns < elapsed ? windowRc6Ns : elapsed;
		activePct = static_cast<uint32_t>((elapsed - slept) * 100 / elapsed);
		lastNs = nowNs; windowRc6Ns = 0;
		const uint32_t scaled = activePct >= FullAtActivePct ? hi :
			floor + (hi - floor) * activePct / FullAtActivePct;
		if (scaled >= target) target = scaled;
		else target = target - floor > StepDown ? target - StepDown : floor;
		if (target < scaled) target = scaled;
		return target;
	}
};

} // namespace TGLRPS
