//
//  kern_tgl_reset.hpp
//  WhateverXe
//
//  Bounded engine reset handshake under host scheduling.
//

#pragma once
#include <stdint.h>

// With -disablegfxfirmware there is no GuC to reset a hung engine. The donor's
// IGHardwareRingBuffer::resetGraphicsEngine writes the engine's GDRST domain and
// then waits for it to clear with no time limit, and it skips the Gen12
// RING_RESET_CTL handshake. A media kernel that never retires (seen from
// VTDecoderXPCService on RCS) left the reset waiting and the display frozen.
// Request the reset, wait boundedly for READY_TO_RESET, reset the domain, and
// report a domain that never clears instead of waiting forever.
namespace TGLReset {

constexpr unsigned Engines = 6;
constexpr uint32_t GDRST = 0x941C;
constexpr uint32_t Head = 0x34, Acthd = 0x74, MiMode = 0x9C, ResetCtl = 0xD0;
constexpr uint32_t StopRing = 1u << 8;
constexpr uint32_t RequestReset = 1u << 0, ReadyToReset = 1u << 1;
constexpr unsigned PollUs = 10, ReadyTimeoutUs = 1000, DomainTimeoutUs = 50000;

// kIGHwCsDesc engine IDs: 0 RCS, 1 CCS, 2 BCS, 3 VCS0, 4 VCS2, 5 VECS0.
constexpr uint32_t Base[Engines] {0x2000, 0x1A000, 0x22000, 0x1C0000, 0x1D0000, 0x1C8000};
// Gen11+ GDRST domains. Tiger Lake has no compute engine to reset.
constexpr uint32_t Domain[Engines] {1u << 1, 0, 1u << 2, 1u << 5, 1u << 7, 1u << 13};

constexpr uint32_t maskedSet(uint32_t bits) { return bits << 16 | bits; }
constexpr uint32_t maskedClear(uint32_t bits) { return bits << 16; }

enum class Result : uint32_t { Done, DomainStuck, Invalid };

struct Report {
	Result result {Result::Invalid};
	bool ready {};
	uint32_t head {}, acthd {}, waitedUs {};
};

// IO: uint32_t read(uint32_t offset); void write(uint32_t offset, uint32_t value);
// void delayUs(unsigned). The caller holds forcewake.
template <class IO>
Report reset(IO &io, unsigned engine) {
	Report r;
	if (engine >= Engines || !Domain[engine]) return r;
	const uint32_t base = Base[engine], domain = Domain[engine];
	r.head = io.read(base + Head);
	r.acthd = io.read(base + Acthd);
	io.write(base + MiMode, maskedSet(StopRing));
	io.write(base + ResetCtl, maskedSet(RequestReset));
	for (unsigned t = 0;; t += PollUs) {
		if (io.read(base + ResetCtl) & ReadyToReset) { r.ready = true; break; }
		if (t >= ReadyTimeoutUs) break;
		io.delayUs(PollUs);
	}
	// A hung engine may never report ready; the domain reset is still the
	// only way to stop it.
	io.write(GDRST, domain);
	r.result = Result::DomainStuck;
	for (unsigned t = 0;; t += PollUs) {
		if (!(io.read(GDRST) & domain)) { r.result = Result::Done; r.waitedUs = t; break; }
		if (t >= DomainTimeoutUs) { r.waitedUs = t; break; }
		io.delayUs(PollUs);
	}
	io.write(base + ResetCtl, maskedClear(RequestReset));
	io.write(base + MiMode, maskedClear(StopRing));
	return r;
}

} // namespace TGLReset
