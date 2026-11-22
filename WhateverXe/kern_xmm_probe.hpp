// -wexxmmprobe: a read-only look at the Intel XMM 7360 LTE modem (PCI 8086:7360) to tell whether its
// firmware is running. Published as the IGPU property "WEXXmmProbe".
//
// The Linux driver xmm7360-pci reads one word, BAR2[0], before touching anything else: 0x600df00d means the
// modem firmware is up, 0xfeedb007 means the modem is still in its boot ROM (no firmware), anything else is
// unknown. BAR2[0x60] is the driver's MODE word (only polled there). Nothing is written to the BARs. By
// default nothing is written to the config space either, and when the command register has memory space off
// the BAR is not read. With -wexxmmenable the command register's memory-space bit is set for the duration of
// one read (only when the device is in D0) and the original command value is restored right after.
#pragma once

#include <Headers/kern_api.hpp>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSSerialize.h>
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>

namespace WEXXmm {
	// Layout (little endian): u32 revision (1), u32 found, u32 status (0 ok, 1 no device, 2 memory decode off,
	// 3 BAR2 unusable, 4 map failed), u32 command, u32 cfg[16] (config dwords 0..15), u64 bar2Phys,
	// u32 bar2Status (BAR2[0]), u32 bar2Mode (BAR2[0x60]).
	static IOPCIDevice *dev {};
	static volatile uint32_t *bar2 {};
	static uint64_t bar2Phys {};
	static uint32_t mapStatus {1};
	static bool enableMem {};

	static inline void find() {
		if (dev) return;
		auto matching = IOService::serviceMatching("IOPCIDevice");
		if (!matching) return;
		auto it = IOService::getMatchingServices(matching);
		matching->release();
		if (!it) return;
		while (auto *obj = it->getNextObject()) {
			auto *d = static_cast<IOPCIDevice *>(obj);
			if (d->configRead16(kIOPCIConfigVendorID) == 0x8086 && d->configRead16(kIOPCIConfigDeviceID) == 0x7360) {
				dev = d;
				dev->retain();
				break;
			}
		}
		it->release();
	}

	static inline bool serialize(void *, void *, OSSerialize *serializer) {
		struct Blob { uint32_t revision, found, status, command; uint32_t cfg[64]; uint64_t bar2Phys; uint32_t bar2Status, bar2Mode, enabledTemporarily, pad; } blob {};
		blob.revision = 2;
		find();
		bool restore = false;
		uint16_t origCmd = 0;
		if (!dev) {
			blob.status = 1;
		} else {
			blob.found = 1;
			blob.command = dev->configRead16(kIOPCIConfigCommand);
			origCmd = static_cast<uint16_t>(blob.command);
			for (unsigned i = 0; i < 64; ++i)
				blob.cfg[i] = dev->configRead32(static_cast<IOByteCount>(i * 4));
			if (!(blob.command & 0x2) && enableMem) {
				// PM capability at 0x40: PMCSR at 0x44, bits 1:0 = D state; only touch a device in D0.
				const uint32_t pmcsr = blob.cfg[0x44 / 4];
				if ((blob.cfg[0x34 / 4] & 0xFF) == 0x40 && !(pmcsr & 3)) {
					dev->configWrite16(kIOPCIConfigCommand, static_cast<uint16_t>(origCmd | 0x2));
					restore = true;
					blob.enabledTemporarily = 1;
				}
			}
			if (!((blob.command & 0x2) || restore)) {
				blob.status = 2;
			} else {
				const uint32_t lo = blob.cfg[6];   // BAR2 at config 0x18
				const bool mem = !(lo & 1), is64 = mem && ((lo >> 1) & 3) == 2;
				uint64_t base = lo & ~0xFULL;
				if (is64) base |= static_cast<uint64_t>(blob.cfg[7]) << 32;
				blob.bar2Phys = base;
				if (!mem || !base) {
					blob.status = 3;
				} else {
					if (!bar2) {
						if (auto *md = IOMemoryDescriptor::withPhysicalAddress(base, 0x1000, kIODirectionNone)) {
							if (auto *map = md->map(kIOMapInhibitCache))
								bar2 = reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
						}
					}
					if (!bar2) {
						blob.status = 4;
					} else {
						blob.bar2Status = bar2[0];
						blob.bar2Mode = bar2[0x60 / 4];
					}
				}
			}
		}
		if (restore) dev->configWrite16(kIOPCIConfigCommand, origCmd);
		auto data = OSData::withBytes(&blob, sizeof(blob));
		if (!data) return false;
		const bool ok = data->serialize(serializer);
		data->release();
		return ok;
	}
}
