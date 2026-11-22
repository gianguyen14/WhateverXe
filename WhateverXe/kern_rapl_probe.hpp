// -wexraplprobe: a read-only view of the package power-management MSRs, published as the IGPU property
// "WEXRaplProbe". Purpose: find out who changes the package power limit on this laptop. Under macOS the
// CPU alternates between a high limit and about 6 W / 400 MHz every minute or two under sustained load,
// and Windows/Linux do not show it, so the live PL1/PL2 and the limit-reason registers are needed.
//
// Nothing is written. Each MSR is read with the kernel's rdmsr_carefully(), which returns an error
// instead of faulting when the MSR does not exist; the symbol is resolved at run time so a kernel
// without it only disables the probe. The property is a serializer evaluated when ioreg reads it.
#pragma once

#include <Headers/kern_api.hpp>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSSerialize.h>
#include <libkern/c++/OSNumber.h>
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <mach/mach_time.h>

namespace WEXRapl {
	// Layout of WEXRaplProbe (little endian):
	//   u32 revision (2), u32 count, u64 mach_absolute_time, then count records of
	//   { u32 msr, u32 status (0 = read, 1 = not readable, 2 = probe unavailable), u32 lo, u32 hi },
	//   then u32 ecCount and ecCount records of { u32 id = (method << 8) | sensor, u32 status, u64 value },
		//   then (revision 3) u32 mmioStatus, mmioBaseLo, mmioBaseHi, mmioCount and mmioCount dwords of MCHBAR+0x5000+0x920.
	static const uint32_t Msrs[] {
		0x0CE,  // MSR_PLATFORM_INFO
		0x198,  // IA32_PERF_STATUS (current ratio)
		0x19C,  // IA32_THERM_STATUS
		0x1A2,  // MSR_TEMPERATURE_TARGET
		0x1AD,  // MSR_TURBO_RATIO_LIMIT
		0x1B0,  // IA32_ENERGY_PERF_BIAS
		0x1B1,  // IA32_PACKAGE_THERM_STATUS
		0x1FC,  // MSR_POWER_CTL (PROCHOT, EET, ...)
		0x606,  // MSR_RAPL_POWER_UNIT
		0x610,  // MSR_PKG_POWER_LIMIT (PL1, PL2, windows, lock)
		0x611,  // MSR_PKG_ENERGY_STATUS
		0x614,  // MSR_PKG_POWER_INFO
		0x638,  // MSR_PP0_POWER_LIMIT
		0x640,  // MSR_PP1_POWER_LIMIT
		0x64F,  // MSR_CORE_PERF_LIMIT_REASONS
		0x65C,  // MSR_PLATFORM_POWER_LIMIT (PL1/PL2 of the whole platform)
		0x6B0,  // MSR_GRAPHICS_PERF_LIMIT_REASONS
		0x6B1,  // MSR_RING_PERF_LIMIT_REASONS
		0x770,  // IA32_PM_ENABLE (HWP)
		0x771,  // IA32_HWP_CAPABILITIES
		0x774,  // IA32_HWP_REQUEST
		0x777   // IA32_HWP_STATUS
	};
	static const unsigned Count = sizeof(Msrs) / sizeof(Msrs[0]);
	static int (*readCarefully)(uint32_t, uint32_t *, uint32_t *) {};

	// Embedded controller view through the firmware's own DPTF helpers (SSDT "DptfTabl", scope
	// \_SB.PC00.LPCB.ECDV): DPRT = EC register 0x32 (DPTF mode), KDRT(i) = temperature of EC sensor i
	// in degrees C (0 CPU, 1 skin HT1, 2 memory, 3 M.2), DRTL/DRTH/DRHY(i) = low/high threshold and
	// hysteresis the EC holds for sensor i, DSRQ = EC register 0x38 (request queue). Read-only.
	static IOACPIPlatformDevice *ec {};
	enum EcMethod : uint32_t { DPRT = 1, KDRT = 2, DRTL = 3, DRTH = 4, DRHY = 5, DSRQ = 6 };
	static const struct { EcMethod m; const char *name; bool arg; } EcCalls[] {
		{DPRT, "DPRT", false}, {DSRQ, "DSRQ", false},
		{KDRT, "KDRT", true}, {DRTL, "DRTL", true}, {DRTH, "DRTH", true}, {DRHY, "DRHY", true}
	};
	static const unsigned EcSensors = 4;

	// Memory-mapped copy of the package power registers: the MCHBAR of the host bridge (PCI 0:0.0, config
	// 0x48) at offset 0x5000 is what the firmware's CPWR region covers; 0x938 is the RAPL unit, 0x9A0/0x9A4
	// the PL1/PL2 copy that SSDT-PL1 and the DPTF code write. Dumped as raw dwords (read-only mapping use).
	static const unsigned MmioFirst = 0x920, MmioCount = 64;
	static volatile uint32_t *mmio {};
	static uint32_t mmioStatus {2}, mmioBaseLo {}, mmioBaseHi {};

	static inline void mapMmio() {
		if (mmio || mmioStatus != 2) return;
		mmioStatus = 1;
		auto matching = IOService::serviceMatching("IOPCIDevice");
		if (!matching) return;
		auto it = IOService::getMatchingServices(matching);
		matching->release();
		if (!it) return;
		while (auto *obj = it->getNextObject()) {
			auto *dev = static_cast<IOPCIDevice *>(obj);
			if (dev->getBusNumber() || dev->getDeviceNumber() || dev->getFunctionNumber()) continue;
			const uint32_t lo = dev->configRead32(0x48), hi = dev->configRead32(0x4C);
			mmioBaseLo = lo; mmioBaseHi = hi;
			const uint64_t base = ((static_cast<uint64_t>(hi) << 32) | lo) & 0x7FFFFF8000ULL;
			if ((lo & 1) && base) {
				if (auto *md = IOMemoryDescriptor::withPhysicalAddress(base + 0x5000, 0x1000, kIODirectionNone)) {
					if (auto *map = md->map(kIOMapInhibitCache)) {
						mmio = reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
						mmioStatus = 0;
					}
				}
			}
			break;
		}
		it->release();
	}

	static inline void init(KernelPatcher &patcher) {
		readCarefully = reinterpret_cast<decltype(readCarefully)>(
			patcher.solveSymbol(KernelPatcher::KernelID, "_rdmsr_carefully"));
		patcher.clearError();
		if (auto matching = IOService::serviceMatching("IOACPIPlatformDevice")) {
			if (auto it = IOService::getMatchingServices(matching)) {
				while (auto *obj = it->getNextObject()) {
					auto *svc = static_cast<IOService *>(obj);
					const char *n = svc->getName();
					if (n && !strcmp(n, "ECDV")) {
						ec = static_cast<IOACPIPlatformDevice *>(svc);
						ec->retain();
						break;
					}
				}
				it->release();
			}
			matching->release();
		}
	}

	// -wexecdptf / -wexecdptf0: one write of the DPTF mode register through the firmware's own DPST(), the
	// call the firmware makes when an OS reports that a DPTF driver is present. Returns the register after.
	static inline void ecSetDptf(uint32_t mode) {
		if (!ec) return;
		UInt64 v = 0;
		if (auto *n = OSNumber::withNumber(static_cast<unsigned long long>(mode), 32)) {
			OSObject *params[1] {n};
			ec->evaluateInteger("DPST", &v, params, 1);
			n->release();
		}
	}

	static inline uint32_t ecRead(const char *name, bool hasArg, uint32_t arg, uint64_t *out) {
		if (!ec) return 2;
		UInt64 v = 0;
		IOReturn rc;
		if (hasArg) {
			OSNumber *n = OSNumber::withNumber(static_cast<unsigned long long>(arg), 32);
			if (!n) return 3;
			OSObject *params[1] {n};
			rc = ec->evaluateInteger(name, &v, params, 1);
			n->release();
		} else {
			rc = ec->evaluateInteger(name, &v);
		}
		if (rc != kIOReturnSuccess) return 1;
		*out = v;
		return 0;
	}

	static inline bool serialize(void *, void *, OSSerialize *serializer) {
		struct Record { uint32_t msr, status, lo, hi; };
		struct EcRecord { uint32_t id, status; uint64_t value; };
		static const unsigned EcCount = 2 + 4 * EcSensors;
		struct Blob { uint32_t revision, count; uint64_t time; Record rec[Count]; uint32_t ecCount; uint32_t pad; EcRecord ecRec[EcCount]; uint32_t mmioStatus, mmioBaseLo, mmioBaseHi, mmioCount; uint32_t mmio[MmioCount]; } blob {};
		blob.revision = 3;
		blob.count = Count;
		blob.time = mach_absolute_time();
		for (unsigned i = 0; i < Count; ++i) {
			blob.rec[i].msr = Msrs[i];
			if (!readCarefully) {
				blob.rec[i].status = 2;
			} else {
				uint32_t lo = 0, hi = 0;
				const int rc = readCarefully(Msrs[i], &lo, &hi);
				blob.rec[i].status = rc ? 1 : 0;
				blob.rec[i].lo = rc ? 0 : lo;
				blob.rec[i].hi = rc ? 0 : hi;
			}
		}
		blob.ecCount = EcCount;
		unsigned k = 0;
		for (const auto &c : EcCalls) {
			const unsigned n = c.arg ? EcSensors : 1;
			for (unsigned i = 0; i < n; ++i) {
				uint64_t v = 0;
				blob.ecRec[k].id = (static_cast<uint32_t>(c.m) << 8) | i;
				blob.ecRec[k].status = ecRead(c.name, c.arg, i, &v);
				blob.ecRec[k].value = blob.ecRec[k].status ? 0 : v;
				++k;
			}
		}
		mapMmio();
		blob.mmioStatus = mmioStatus;
		blob.mmioBaseLo = mmioBaseLo;
		blob.mmioBaseHi = mmioBaseHi;
		blob.mmioCount = MmioCount;
		if (mmio) for (unsigned i = 0; i < MmioCount; ++i) blob.mmio[i] = mmio[MmioFirst / 4 + i];
		auto data = OSData::withBytes(&blob, sizeof(blob));
		if (!data) return false;
		const bool ok = data->serialize(serializer);
		data->release();
		return ok;
	}
}
