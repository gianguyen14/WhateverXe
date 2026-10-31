// CPU-only tests: clang++ -std=c++17 -fsanitize=address,undefined tests/tgl_rps.cpp
#include "../WhateverXe/kern_tgl_rps.hpp"
#include <cassert>
#include <cstdio>

using namespace TGLRPS;

int main() {
	// i5-1145G7: RP0 1300 MHz, RP1 350 MHz, RPn 100 MHz.
	const Caps caps = decodeCaps(0x0002071A);
	assert(caps.rp0 == 26 && caps.rp1 == 7 && caps.rpn == 2);

	const uint64_t ms = 1000 * 1000;
	auto rc6For = [](uint64_t ns) { return static_cast<uint32_t>(ns / RC6UnitNs); };

	// Starts at RP0; the first call only primes the window.
	Governor g;
	uint64_t t = 1000 * ms;
	uint32_t rc6 = 0;
	assert(g.step(t, rc6, caps) == 26);

	// Fully idle (always in RC6): steps down 100 MHz per window to RP1.
	uint32_t f = 26;
	for (int i = 0; i < 20; ++i) {
		t += 50 * ms; rc6 += rc6For(50 * ms);
		const uint32_t next = g.step(t, rc6, caps);
		assert(next <= f && f - next <= StepDown);
		f = next;
	}
	assert(f == 7 && g.activePct == 0);

	// Sustained load (never in RC6): back to RP0 within one window.
	t += 50 * ms;
	assert(g.step(t, rc6, caps) == 26 && g.activePct == 100);

	// Calls inside a window keep the current target.
	t += 10 * ms;
	assert(g.step(t, rc6, caps) == 26);

	// Half busy settles in between floor and RP0.
	for (int i = 0; i < 20; ++i) {
		t += 50 * ms; rc6 += rc6For(25 * ms);
		f = g.step(t, rc6, caps);
	}
	assert(g.activePct == 50 && f == 7 + (26 - 7) * 50 / 80);

	// RC6 counter wrap is handled; more sleep than elapsed counts as idle.
	Governor w;
	t = 5000 * ms; rc6 = 0xFFFFFFF0u;
	w.step(t, rc6, caps);
	t += 50 * ms; rc6 += rc6For(50 * ms);
	assert(w.step(t, rc6, caps) == 24 && w.activePct == 0);
	t += 50 * ms; rc6 += rc6For(500 * ms);
	assert(w.step(t, rc6, caps) == 22 && w.activePct == 0);

	// Time going backwards re-primes without changing the request.
	assert(w.step(t - ms, rc6, caps) == 22);

	// Caps without RP1 use RPn; RP1 above RP0 is clamped.
	Governor n;
	const Caps noRp1 = decodeCaps(0x0002001A);
	n.step(1 * ms, 0, noRp1);
	for (int i = 1; i <= 20; ++i) n.step((1 + 50 * i) * ms, rc6For(50 * ms) * i, noRp1);
	assert(n.target == 2);
	Governor c;
	assert(c.step(1 * ms, 0, Caps {10, 20, 2}) == 10);

	puts("tgl_rps ok");
	return 0;
}
