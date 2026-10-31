//
//  kern_diag.cpp
//  WhateverXe
//

#include "kern_diag.hpp"
#include <Headers/kern_api.hpp>

static IORegistryEntry *diagRoot;

IORegistryEntry *wexDiagRoot() {
	if (auto entry = __atomic_load_n(&diagRoot, __ATOMIC_ACQUIRE))
		return entry;
	auto root = IORegistryEntry::getRegistryRoot();
	IORegistryEntry *entry = root;
	if (!checkKernelArgument("-wexdiag")) {
		entry = OSTypeAlloc(IORegistryEntry);
		if (!entry || !entry->init()) {
			OSSafeReleaseNULL(entry);
			entry = root;
		}
	}
	IORegistryEntry *expected = nullptr;
	if (!__atomic_compare_exchange_n(&diagRoot, &expected, entry, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		if (entry != root)
			entry->release();
		return expected;
	}
	return entry;
}
