// -wexxmmpsi / -wexxmmpsigo: the boot-ROM PSI download of the Intel XMM 7360 modem, ported from the Linux iosm
// driver (drivers/net/wwan/iosm: ipc_imem_sys_psi_transfer and ipc_mmio_*), reduced to what the ROM phase needs.
//
// In the ROM phase the driver builds no ring and uses no interrupt: it puts the PSI in a DMA buffer, writes its
// 64-bit address and 32-bit size to BAR2 (+0x54, +0x5C), writes the doorbell (BAR0 +0x10) with the value
// 0xFEEDB007, and waits for the ROM exit code (BAR2 +0x40: 0x01 or 0x10 means accepted) and for the execution
// stage (BAR2 +0x00) to become 0xFEEDBEEF. This code does the same by polling.
//
// -wexxmmpsi    arms two IGPU properties, each acting once, on its first read (no reboot between them):
//   WEXXmmPsi    dry run: allocates and maps the buffer through the device's IOMMU mapper and reports the address
//                the device would see. Writes nothing to the device.
//   WEXXmmPsiGo  real run: also enables bus mastering, writes the two scratchpad registers and rings the doorbell.
// Nothing is flashed: the PSI only runs from the modem's RAM and is gone at the next power cycle.
#pragma once

#include "kern_xmm_probe.hpp"
#include <Headers/kern_file.hpp>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODMACommand.h>
#include <IOKit/IOMapper.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>

#if __has_include("kern_xmm_psi_data.h")
#include "kern_xmm_psi_data.h"
#define WEX_HAVE_XMM_PSI 1
#else
#define WEX_HAVE_XMM_PSI 0
#endif

namespace WEXXmm {
	static bool psiArmed {};
	static bool psiDoneDry {}, psiDoneGo {};
	// Result layout (little endian), revision 1:
	// u32 rev, mode (1 dry, 2 go), status, cmdBefore, cmdAfter, stageBefore, romExitBefore, stageAfter,
	// romExitAfter, polls, dmaStatus, size, u64 iova
	struct PsiBlob { uint32_t rev, mode, status, cmdBefore, cmdAfter, stageBefore, romBefore, stageAfter, romAfter, polls, dmaStatus, size; uint64_t iova; };
	static PsiBlob psiResDry {}, psiResGo {};
	static IOBufferMemoryDescriptor *psiMd {};
	static IODMACommand *psiCmd {};

	// status: 0 ok, 1 no device, 2 BARs unavailable, 3 no PSI data, 4 not in ROM stage, 5 DMA setup failed,
	// 6 ROM rejected the image (romAfter), 7 timeout waiting for ROM exit code, 8 stage did not become PSI,
	// 9 dry run done
	static inline void mapBars(uint64_t b0, uint64_t b2) {
		if (!bar0)
			if (auto *md = IOMemoryDescriptor::withPhysicalAddress(b0, 0x1000, kIODirectionNone))
				if (auto *map = md->map(kIOMapInhibitCache))
					bar0 = reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
		if (!bar2)
			if (auto *md = IOMemoryDescriptor::withPhysicalAddress(b2, 0x1000, kIODirectionNone))
				if (auto *map = md->map(kIOMapInhibitCache))
					bar2 = reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
	}

	static inline void psiRun(bool psiGo) {
		PsiBlob &psiRes = psiGo ? psiResGo : psiResDry;
		(psiGo ? psiDoneGo : psiDoneDry) = true;
		psiRes = {};
		psiRes.rev = 1;
		psiRes.mode = psiGo ? 2 : 1;
		find();
		if (!dev) { psiRes.status = 1; return; }
#if !WEX_HAVE_XMM_PSI
		psiRes.status = 3; return;
#else
		psiRes.size = xmmPsiSize;
		const uint32_t cfg4 = dev->configRead32(0x10), cfg5 = dev->configRead32(0x14), cfg6 = dev->configRead32(0x18), cfg7 = dev->configRead32(0x1C);
		const uint64_t b0 = (static_cast<uint64_t>(cfg5) << 32 | cfg4) & ~0xFULL, b2 = (static_cast<uint64_t>(cfg7) << 32 | cfg6) & ~0xFULL;
		const uint16_t cmd = dev->configRead16(kIOPCIConfigCommand);
		psiRes.cmdBefore = cmd;
		if (!b0 || !b2) { psiRes.status = 2; return; }
		// Memory decode must be on to read the BARs; only touched when the device is in D0 (see probe).
		const uint32_t pmcsr = dev->configRead32(0x44);
		if (pmcsr & 3) { psiRes.status = 2; return; }
		const uint16_t want = static_cast<uint16_t>(cmd | 0x2 | (psiGo ? 0x4 : 0));
		if (want != cmd) dev->configWrite16(kIOPCIConfigCommand, want);
		mapBars(b0, b2);
		if (!bar0 || !bar2) { psiRes.status = 2; if (want != cmd) dev->configWrite16(kIOPCIConfigCommand, cmd); return; }
		psiRes.stageBefore = bar2[0x00 / 4];
		psiRes.romBefore = bar2[0x40 / 4];
		if (psiRes.stageBefore != 0xFEEDB007) { psiRes.status = 4; if (want != cmd) dev->configWrite16(kIOPCIConfigCommand, cmd); return; }
		// DMA buffer, mapped for this device through its IOMMU mapper (or physical when there is none).
		psiMd = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIODirectionOutIn | kIOMemoryPhysicallyContiguous, xmmPsiSize, 0xFFFFFFFFFFFFF000ULL);
		if (psiMd) {
			psiMd->writeBytes(0, xmmPsiData, xmmPsiSize);
			IOMapper *mapper = IOMapper::copyMapperForDevice(dev);
			psiCmd = IODMACommand::withSpecification(IODMACommand::OutputHost64, 64, 0, IODMACommand::kMapped, 0, 1, mapper, nullptr);
			if (psiCmd && psiCmd->setMemoryDescriptor(psiMd) == kIOReturnSuccess && psiCmd->prepare(0, 0, true) == kIOReturnSuccess) {
				UInt64 off = 0; IODMACommand::Segment64 seg {}; UInt32 n = 1;
				if (psiCmd->gen64IOVMSegments(&off, &seg, &n) == kIOReturnSuccess && n == 1 && seg.fLength >= xmmPsiSize) {
					psiRes.iova = seg.fIOVMAddr;
					psiRes.dmaStatus = 0;
				} else psiRes.dmaStatus = 3;
			} else psiRes.dmaStatus = 2;
		} else psiRes.dmaStatus = 1;
		if (psiRes.dmaStatus) { psiRes.status = 5; if (want != cmd) dev->configWrite16(kIOPCIConfigCommand, cmd); return; }
		if (!psiGo) { psiRes.status = 9; psiRes.cmdAfter = dev->configRead16(kIOPCIConfigCommand); if (want != cmd) dev->configWrite16(kIOPCIConfigCommand, cmd); return; }
		// Real run, in the order of ipc_imem_sys_psi_transfer.
		bar2[0x54 / 4] = static_cast<uint32_t>(psiRes.iova);
		bar2[0x58 / 4] = static_cast<uint32_t>(psiRes.iova >> 32);
		bar2[0x5C / 4] = xmmPsiSize;
		OSSynchronizeIO();
		bar0[0x10 / 4] = 0xFEEDB007;   // doorbell 0: IPC_WRITE_PTR_REG_0 (0x10) + irq 0 * IPC_DOORBELL_CH_OFFSET
		OSSynchronizeIO();
		uint32_t rom = 0;
		unsigned polls = 0;
		for (; polls < 300 && !rom; ++polls) { IOSleep(10); rom = bar2[0x40 / 4]; }
		psiRes.polls = polls; psiRes.romAfter = rom;
		if (!rom) psiRes.status = 7;
		else if (rom != 0x01 && rom != 0x10) psiRes.status = 6;
		else {
			uint32_t st = 0;
			for (unsigned i = 0; i < 150; ++i) { st = bar2[0x00 / 4]; if (st == 0xFEEDBEEF) break; IOSleep(20); }
			psiRes.stageAfter = st;
			psiRes.status = (st == 0xFEEDBEEF) ? 0 : 8;
		}
		if (!psiRes.stageAfter) psiRes.stageAfter = bar2[0x00 / 4];
		psiRes.cmdAfter = dev->configRead16(kIOPCIConfigCommand);
#endif
	}

	// The real run only happens when the marker file exists, so that a plain "ioreg -l" (which evaluates every
	// property) can never trigger it by accident.
	static inline bool markerPresent(const char *path = "/private/tmp/xmm_go") {
		size_t sz = 0;
		uint8_t *b = FileIO::readFileToBuffer(path, sz);
		if (!b) return false;
		Buffer::deleter(b);
		return sz > 0;
	}

	static inline bool serializeImpl(bool go, OSSerialize *serializer) {
		static PsiBlob idle {};
		idle = {};
		idle.rev = 1; idle.mode = 2; idle.status = 10;
		if (go && !psiDoneGo && !markerPresent()) {
			auto d = OSData::withBytes(&idle, sizeof(idle));
			if (!d) return false;
			const bool ok = d->serialize(serializer);
			d->release();
			return ok;
		}
		if (!(go ? psiDoneGo : psiDoneDry)) psiRun(go);
		const PsiBlob &r = go ? psiResGo : psiResDry;
		auto data = OSData::withBytes(&r, sizeof(r));
		if (!data) return false;
		const bool ok = data->serialize(serializer);
		data->release();
		return ok;
	}
	static inline bool serializePsi(void *, void *, OSSerialize *serializer) { return serializeImpl(false, serializer); }
	static inline bool serializePsiGo(void *, void *, OSSerialize *serializer) { return serializeImpl(true, serializer); }

	// Reset of the modem through the firmware's own WWAN reset method (\\_SB.PC00.RP05.PXSX.WRST._RST: PERST#, baseband
	// reset, full-card power and clock sequence), then the PCI config space is put back (the reset clears the BARs and the
	// command register) and the exec stage is awaited. Runs once, only when /private/tmp/xmm_reset exists.
	// status: 0 modem back in BOOT stage, 1 no device, 2 no ACPI device, 3 ACPI _RST call failed, 4 config space did not
	// come back, 5 exec stage not BOOT after the wait, 10 not triggered
	struct RstBlob { uint32_t rev, status, acpiRc1, acpiRc2, vendorAfter, cmdAfter, stageAfter, waitMs, chip0, bar0Phys, bar2Phys, pad; };
	static RstBlob rstRes {};
	static bool rstDone {};

	static inline void rstRun() {
		rstDone = true;
		rstRes = {};
		rstRes.rev = 1;
		find();
		if (!dev) { rstRes.status = 1; return; }
		uint32_t saved[64];
		for (unsigned i = 0; i < 64; ++i) saved[i] = dev->configRead32(static_cast<IOByteCount>(i * 4));
		// The PCI device carries "acpi-path" ("IOACPIPlane:/_SB/PC00@0/RP05@1c0000/PXSX@0"); resolve it in the ACPI plane.
		IOACPIPlatformDevice *acpi = nullptr;
		const char *apath = "IOACPIPlane:/_SB/PC00@0/RP05@1c0000/PXSX@0";
		if (auto *ap = OSDynamicCast(OSString, dev->getProperty("acpi-path"))) apath = ap->getCStringNoCopy();
		// metaCast by name: the SDK's metaclass for IOACPIPlatformDevice cannot be resolved from this kext, so
		// OSDynamicCast would always fail (as it did); the ECDV lookup in kern_rapl_probe.hpp uses a static_cast too.
		if (auto *ent = IORegistryEntry::fromPath(apath)) {
			if (ent->metaCast("IOACPIPlatformDevice")) acpi = static_cast<IOACPIPlatformDevice *>(ent);
			else { rstRes.acpiRc1 = 0xDEAD0002; ent->release(); }
		} else rstRes.acpiRc1 = 0xDEAD0001;
		if (!acpi) { rstRes.status = 2; return; }
		OSObject *res = nullptr;
		IOReturn rc = acpi->evaluateObject("WRST._RST", &res);
		rstRes.acpiRc1 = static_cast<uint32_t>(rc);
		if (res) { res->release(); res = nullptr; }
		if (rc != kIOReturnSuccess) {
			rc = acpi->evaluateObject("\\_SB.PC00.RP05.PXSX.WRST._RST", &res);
			rstRes.acpiRc2 = static_cast<uint32_t>(rc);
			if (res) { res->release(); res = nullptr; }
			if (rc != kIOReturnSuccess) { rstRes.status = 3; return; }
		}
		// Wait for the config space to answer again.
		unsigned ms = 0;
		uint32_t v = 0;
		for (; ms < 5000; ms += 20) { v = dev->configRead32(0); if (v == 0x73608086) break; IOSleep(20); }
		rstRes.vendorAfter = v;
		if (v != 0x73608086) { rstRes.status = 4; rstRes.waitMs = ms; return; }
		// Restore the BARs and enable memory decoding (bus mastering stays off until the PSI run).
		for (unsigned off = 0x10; off <= 0x24; off += 4) dev->configWrite32(off, saved[off / 4]);
		dev->configWrite16(kIOPCIConfigCommand, 0x2);
		rstRes.cmdAfter = dev->configRead16(kIOPCIConfigCommand);
		const uint64_t b0 = (static_cast<uint64_t>(saved[5]) << 32 | saved[4]) & ~0xFULL, b2 = (static_cast<uint64_t>(saved[7]) << 32 | saved[6]) & ~0xFULL;
		rstRes.bar0Phys = static_cast<uint32_t>(b0); rstRes.bar2Phys = static_cast<uint32_t>(b2);
		mapBars(b0, b2);
		uint32_t st = 0;
		unsigned t = 0;
		if (bar2) for (; t < 10000; t += 50) { st = bar2[0]; if (st == 0xFEEDB007) break; IOSleep(50); }
		rstRes.stageAfter = st; rstRes.waitMs = ms + t;
		if (bar2) rstRes.chip0 = bar2[1];
		rstRes.status = (st == 0xFEEDB007) ? 0 : 5;
	}

	static inline bool serializeRst(void *, void *, OSSerialize *serializer) {
		static RstBlob idle {};
		idle = {};
		idle.rev = 1; idle.status = 10;
		if (!rstDone && !markerPresent("/private/tmp/xmm_reset")) {
			auto d = OSData::withBytes(&idle, sizeof(idle));
			if (!d) return false;
			const bool ok = d->serialize(serializer);
			d->release();
			return ok;
		}
		if (!rstDone) rstRun();
		auto d = OSData::withBytes(&rstRes, sizeof(rstRes));
		if (!d) return false;
		const bool ok = d->serialize(serializer);
		d->release();
		return ok;
	}
}
