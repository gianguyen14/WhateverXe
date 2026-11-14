// CPU-only tests: clang++ -std=c++17 -fsanitize=address,undefined tests/tgl_context_teardown.cpp
#include "../WhateverXe/kern_tgl_context_teardown.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

using namespace TGLContextTeardown;

namespace {

static const char *KextPath = "/Library/Extensions/AppleIntelTGLGraphics.kext/Contents/MacOS/AppleIntelTGLGraphics";

alignas(16) uintptr_t vtable[64];
alignas(16) uint8_t schedulerObject[0x40];
alignas(16) uint8_t accelerator[OwnerScheduler + 8];
alignas(16) uint8_t context[0x100];
alignas(16) uint8_t task[0x100];

int polls = 0, idleAfter = 0;

bool contextIdle(void *scheduler, void *object) {
	assert(scheduler == schedulerObject && object == context);
	return ++polls >= idleAfter;
}

bool gpuIdle(void *scheduler) {
	assert(scheduler == schedulerObject);
	return ++polls >= idleAfter;
}

void setUp() {
	memset(vtable, 0, sizeof(vtable));
	vtable[IsGpuIdleSlot / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&gpuIdle);
	vtable[IsContextIdleSlot / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(&contextIdle);
	*reinterpret_cast<uintptr_t **>(schedulerObject) = vtable;
	*reinterpret_cast<void **>(accelerator + OwnerScheduler) = schedulerObject;
	*reinterpret_cast<void **>(context + ContextOwner) = accelerator;
	*reinterpret_cast<void **>(task + TaskOwner) = accelerator;
}

void checkScheduler() {
	assert(scheduler(nullptr, ContextOwner) == nullptr);
	assert(scheduler(context, ContextOwner) == schedulerObject);
	assert(scheduler(task, TaskOwner) == schedulerObject);
	uint8_t orphan[0x100] {};
	assert(scheduler(orphan, ContextOwner) == nullptr);
}

void checkDrain() {
	Counters c {};
	drainContext(nullptr, schedulerObject, c);
	drainTask(task, nullptr, c);
	assert(c.contextFrees == 0 && c.taskFrees == 0);

	polls = 0; idleAfter = 1;
	drainContext(context, schedulerObject, c);
	assert(c.contextFrees == 1 && c.contextWaits == 0 && polls == 1);

	polls = 0; idleAfter = 4;
	drainContext(context, schedulerObject, c);
	assert(c.contextFrees == 2 && c.contextWaits == 1 && c.contextDrained == 1 && polls == 4);

	polls = 0; idleAfter = 6;
	drainTask(task, schedulerObject, c);
	assert(c.taskFrees == 1 && c.taskWaits == 1 && c.taskDrained == 1 && polls == 6);

	// A hung engine never reports idle: the wait ends and the native free still runs.
	polls = 0; idleAfter = 1 << 30;
	drainTask(task, schedulerObject, c);
	assert(c.taskFrees == 2 && c.taskWaits == 2 && c.taskDrained == 1 && polls == int(Polls) + 1);

	// A scheduler without the slot is left alone.
	vtable[IsGpuIdleSlot / sizeof(uintptr_t)] = 0;
	polls = 0;
	drainTask(task, schedulerObject, c);
	assert(c.taskFrees == 3 && c.taskWaits == 2 && polls == 0);
}

// The hook is only installed when the installed kext has these exact heads.
void checkInstalledKext() {
	std::ifstream file(KextPath, std::ios::binary);
	if (!file) {
		puts("kext not installed");
		return;
	}
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
	assert(bytes.size() > MinimumSize);
	if (memcmp(bytes.data() + ContextFree, ContextFreeHead, sizeof(ContextFreeHead)) ||
	    memcmp(bytes.data() + TaskFree, TaskFreeHead, sizeof(TaskFreeHead))) {
		puts("different kext build, fix inert");
		return;
	}
	puts("installed kext matches");
}

} // namespace

int main() {
	setUp();
	checkScheduler();
	checkDrain();
	checkInstalledKext();
	puts("tgl_context_teardown passed");
	return 0;
}
