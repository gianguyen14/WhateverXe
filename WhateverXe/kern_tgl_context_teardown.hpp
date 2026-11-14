//
//  kern_tgl_context_teardown.hpp
//  WhateverXe
//
//  Bounded drain before Tiger Lake contexts and tasks are freed (-wexteardownfix).
//

#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef KERNEL
#include <IOKit/IOLib.h>
#else
#include <chrono>
#include <thread>
inline void IODelay(unsigned us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
#endif

// When a process with live GPU contexts ends (a WebRTC call, a killed
// WindowServer), IGHardwareContext::free and IGAccelTask::free unmap the stamp
// and PPGTT pages while MI_FLUSH_DW / PIPE_CONTROL post-sync writes may still be
// in flight. The blitter faults on the write and the GPU resets in a loop
// (b00t0x/WhateverXe#62). Wait for the scheduler to report the context, or for a
// task the GPU, idle before the native free runs. The wait is bounded so a hung
// engine still reaches the native free and recovery.
//
// Ported from starterakun08-tech/WhateverXe@023ede1. Offsets are those of the
// AppleIntelTGLGraphics 16.0.32 kext shipped with WhateverXe Graphics 0.4.x:
// both frees read the accelerator at context +0x50 / task +0x10 and its
// scheduler at +0x1250, and IGScheduler4, IGScheduler5 and IGGuC all place
// isGpuIdle() at vtable +0x160 and isContextIdle() at +0x168.
namespace TGLContextTeardown {

constexpr uint64_t ContextFree = 0x7C5D8;
constexpr uint64_t TaskFree = 0x7D4A;
constexpr uint64_t MinimumSize = 0x7C700;
constexpr uint8_t ContextFreeHead[] {0x55,0x48,0x89,0xE5,0x53,0x50,0x48,0x89,0xFB,0x48,0x8B,0x47,0x40};
constexpr uint8_t TaskFreeHead[] {0x55,0x48,0x89,0xE5,0x53,0x50,0x48,0x89,0xFB,0x48,0x8B,0x47,0x10};
constexpr size_t ContextOwner = 0x50, TaskOwner = 0x10, OwnerScheduler = 0x1250;
constexpr size_t IsGpuIdleSlot = 0x160, IsContextIdleSlot = 0x168;
constexpr unsigned PollUs = 100, Polls = 500;  // at most 50 ms per free

struct Counters {
	uint32_t contextFrees {};
	uint32_t contextWaits {};
	uint32_t contextDrained {};
	uint32_t taskFrees {};
	uint32_t taskWaits {};
	uint32_t taskDrained {};
};

template <typename Idle>
inline void drain(Idle idle, uint32_t &waits, uint32_t &drained) {
	if (idle()) return;
	__atomic_fetch_add(&waits, 1, __ATOMIC_RELAXED);
	for (unsigned i = 0; i < Polls; ++i) {
		IODelay(PollUs);
		if (idle()) {
			__atomic_fetch_add(&drained, 1, __ATOMIC_RELAXED);
			return;
		}
	}
}

inline void *vtableSlot(void *scheduler, size_t slot) {
	auto vtable = *reinterpret_cast<uintptr_t **>(scheduler);
	return vtable ? reinterpret_cast<void *>(vtable[slot / sizeof(uintptr_t)]) : nullptr;
}

inline void *scheduler(void *object, size_t ownerOffset) {
	if (!object) return nullptr;
	auto owner = *reinterpret_cast<uint8_t **>(static_cast<uint8_t *>(object) + ownerOffset);
	return owner ? *reinterpret_cast<void **>(owner + OwnerScheduler) : nullptr;
}

inline void drainContext(void *context, void *scheduler, Counters &counters) {
	if (!context || !scheduler) return;
	__atomic_fetch_add(&counters.contextFrees, 1, __ATOMIC_RELAXED);
	auto isIdle = reinterpret_cast<bool (*)(void *, void *)>(vtableSlot(scheduler, IsContextIdleSlot));
	if (!isIdle) return;
	drain([&] { return isIdle(scheduler, context); }, counters.contextWaits, counters.contextDrained);
}

inline void drainTask(void *task, void *scheduler, Counters &counters) {
	if (!task || !scheduler) return;
	__atomic_fetch_add(&counters.taskFrees, 1, __ATOMIC_RELAXED);
	auto isGpuIdle = reinterpret_cast<bool (*)(void *)>(vtableSlot(scheduler, IsGpuIdleSlot));
	if (!isGpuIdle) return;
	drain([&] { return isGpuIdle(scheduler); }, counters.taskWaits, counters.taskDrained);
}

} // namespace TGLContextTeardown
