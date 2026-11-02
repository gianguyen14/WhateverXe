// CPU-only tests: clang++ -std=c++17 -fsanitize=address,undefined tests/tgl_reset.cpp
#include "../WhateverXe/kern_tgl_reset.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>

using namespace TGLReset;

// Register file model: READY_TO_RESET follows a pending request after
// `readyAfterUs`; GDRST domain bits self-clear after `clearAfterUs`.
struct MockIO {
	std::map<uint32_t, uint32_t> regs;
	std::vector<std::pair<uint32_t, uint32_t>> writes;
	unsigned now = 0, readyAfterUs = 30, clearAfterUs = 40, requestAt = ~0u, resetAt = ~0u;
	bool neverReady = false, neverClear = false;
	uint32_t read(uint32_t off) {
		if (off == GDRST) return resetAt != ~0u && (neverClear || now < resetAt + clearAfterUs) ? regs[GDRST] : 0;
		for (auto base : Base)
			if (off == base + ResetCtl)
				return regs[off] | (!neverReady && requestAt != ~0u && now >= requestAt + readyAfterUs ? ReadyToReset : 0);
		return regs[off];
	}
	void write(uint32_t off, uint32_t value) {
		writes.push_back({off, value});
		if (off == GDRST) { regs[GDRST] = value; resetAt = now; return; }
		const uint32_t mask = value >> 16, bits = value & mask;
		regs[off] = (regs[off] & ~mask) | bits;
		for (auto base : Base)
			if (off == base + ResetCtl) requestAt = bits & RequestReset ? now : ~0u;
	}
	void delayUs(unsigned us) { now += us; }
};

int main() {
	assert(maskedSet(RequestReset) == 0x00010001 && maskedClear(RequestReset) == 0x00010000);
	assert(maskedSet(StopRing) == 0x01000100 && maskedClear(StopRing) == 0x01000000);

	// RCS: handshake, render domain reset, request and stop released after.
	MockIO io;
	io.regs[0x2000 + Head] = 0xA8;
	io.regs[0x2000 + Acthd] = 0x402DB0C0;
	auto r = reset(io, 0);
	assert(r.result == Result::Done && r.ready && r.head == 0xA8 && r.acthd == 0x402DB0C0);
	assert(io.writes.size() == 5);
	assert(io.writes[0] == std::make_pair(0x2000 + MiMode, maskedSet(StopRing)));
	assert(io.writes[1] == std::make_pair(0x2000 + ResetCtl, maskedSet(RequestReset)));
	assert(io.writes[2] == std::make_pair(GDRST, 1u << 1));
	assert(io.writes[3] == std::make_pair(0x2000 + ResetCtl, maskedClear(RequestReset)));
	assert(io.writes[4] == std::make_pair(0x2000 + MiMode, maskedClear(StopRing)));
	assert(r.waitedUs >= 40 && r.waitedUs <= 40 + PollUs);

	// Engine never ready: still reset, within the bounded wait.
	MockIO stuck;
	stuck.neverReady = true;
	r = reset(stuck, 0);
	assert(r.result == Result::Done && !r.ready);
	assert(stuck.now <= ReadyTimeoutUs + 40 + 2 * PollUs);

	// Domain never clears: bounded, reported, request still released.
	MockIO hung;
	hung.neverClear = true;
	r = reset(hung, 0);
	assert(r.result == Result::DomainStuck && r.waitedUs == DomainTimeoutUs);
	assert(hung.now <= ReadyTimeoutUs + DomainTimeoutUs + 2 * PollUs);
	assert(hung.writes.back() == std::make_pair(0x2000 + MiMode, maskedClear(StopRing)));

	// Other engines use their own base and domain; CCS and bad IDs do nothing.
	MockIO vcs;
	assert(reset(vcs, 3).result == Result::Done && vcs.writes[2] == std::make_pair(GDRST, 1u << 5));
	assert(vcs.writes[1].first == 0x1C0000 + ResetCtl);
	MockIO vecs;
	assert(reset(vecs, 5).result == Result::Done && vecs.writes[2] == std::make_pair(GDRST, 1u << 13));
	MockIO none;
	assert(reset(none, 1).result == Result::Invalid && none.writes.empty());
	assert(reset(none, Engines).result == Result::Invalid && none.writes.empty());

	puts("tgl_reset ok");
	return 0;
}
