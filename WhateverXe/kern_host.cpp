// Copyright © 2018 vit9696. Derived from WhateverGreen (BSD-3-Clause).
#include "kern_host.hpp"
#include "kern_diag.hpp"
#include "kern_xelp_device.hpp"
#include "kern_xe_data_access.hpp"
#include "kern_backlight_profile.hpp"
#include "kern_tgl_page_fix.hpp"
#include <Headers/kern_api.hpp>
#include <Headers/kern_mach.hpp>
#include <Headers/kern_patcher.hpp>
#include <sys/vnode.h>
#include <Headers/kern_iokit.hpp>
#include <IOKit/pci/IOPCIDevice.h>
XeHost *XeHost::callbackHost;
struct FramebufferViewer : public IOFramebuffer {
 static IOMemoryMap *&getVramMap(IOFramebuffer *fb) { return static_cast<FramebufferViewer *>(fb)->fVramMap; }
};
static const char *graphicsPath[] { "/System/Library/Extensions/IOGraphicsFamily.kext/IOGraphicsFamily" };
static KernelPatcher::KextInfo graphics { "com.apple.iokit.IOGraphicsFamily",graphicsPath,arrsize(graphicsPath),{true},{},KernelPatcher::KextInfo::Unloaded };
static const char *backlightPath[] { "/System/Library/Extensions/AppleBacklight.kext/Contents/MacOS/AppleBacklight" };
static KernelPatcher::KextInfo backlight { "com.apple.driver.AppleBacklight",backlightPath,arrsize(backlightPath),{true},{},KernelPatcher::KextInfo::Unloaded };
namespace {
using PageValidator=void (*)(vnode_t,memory_object_t,memory_object_offset_t,const void *,int *,int *,int *);
PageValidator orgValidatePage;
bool pageFixEnabled[TGLPageFix::KindCount],pageFixReady;
uint64_t pageFixSeen[TGLPageFix::KindCount],pageFixPatched[TGLPageFix::KindCount];
// Runs after the kernel's check of each signed page; see kern_tgl_page_fix.hpp.
void validatePage(vnode_t vnode,memory_object_t pager,memory_object_offset_t offset,const void *data,int *validated,int *tainted,int *nx) {
 orgValidatePage(vnode,pager,offset,data,validated,tainted,nx);
 if(!validated || !tainted || !nx || *validated!=0xf || *tainted || *nx)return;
 const auto kind=TGLPageFix::classify(data,TGLPageFix::PageSize,offset);
 if(kind==TGLPageFix::None)return;
 __atomic_fetch_add(&pageFixSeen[kind],1ULL,__ATOMIC_RELAXED);
 if(!pageFixEnabled[kind] || MachInfo::setKernelWriting(true,KernelPatcher::kernelWriteLock)!=KERN_SUCCESS)return;
 if(TGLPageFix::classify(data,TGLPageFix::PageSize,offset)==kind) {
  TGLPageFix::apply(kind,const_cast<void *>(data));
  __atomic_fetch_add(&pageFixPatched[kind],1ULL,__ATOMIC_RELAXED);
 }
 MachInfo::setKernelWriting(false,KernelPatcher::kernelWriteLock);
}
bool pageFixSerialize(void *,void *,OSSerialize *serializer) {
 const uint64_t q[] {3,pageFixReady,pageFixEnabled[TGLPageFix::VA],pageFixEnabled[TGLPageFix::MTL],
  __atomic_load_n(&pageFixSeen[TGLPageFix::VA],__ATOMIC_RELAXED),__atomic_load_n(&pageFixPatched[TGLPageFix::VA],__ATOMIC_RELAXED),
  __atomic_load_n(&pageFixSeen[TGLPageFix::MTL],__ATOMIC_RELAXED),__atomic_load_n(&pageFixPatched[TGLPageFix::MTL],__ATOMIC_RELAXED),
  __atomic_load_n(&pageFixSeen[TGLPageFix::XFER],__ATOMIC_RELAXED),__atomic_load_n(&pageFixPatched[TGLPageFix::XFER],__ATOMIC_RELAXED),
  // Schema 3 appends the null guard: enabled, then seen and patched for its three windows.
  pageFixEnabled[TGLPageFix::NullGuardStatus],
  __atomic_load_n(&pageFixSeen[TGLPageFix::NullGuardStatus],__ATOMIC_RELAXED),__atomic_load_n(&pageFixPatched[TGLPageFix::NullGuardStatus],__ATOMIC_RELAXED),
  __atomic_load_n(&pageFixSeen[TGLPageFix::NullGuardPrepare],__ATOMIC_RELAXED),__atomic_load_n(&pageFixPatched[TGLPageFix::NullGuardPrepare],__ATOMIC_RELAXED),
  __atomic_load_n(&pageFixSeen[TGLPageFix::NullGuardRender],__ATOMIC_RELAXED),__atomic_load_n(&pageFixPatched[TGLPageFix::NullGuardRender],__ATOMIC_RELAXED)};
 auto data=OSData::withBytes(q,sizeof(q));if(!data)return false;
 const bool ok=data->serialize(serializer);data->release();return ok;
}
// -wexvaprobe only counts matching pages; it wins over the fixes. -wexnullguard
// replaces the pixel-transfer part of -wexvafix (XFER), which would otherwise make
// every scaler context fail again.
void preparePageFix(KernelPatcher &patcher,IOPCIDevice *gpu,const XELP::Profile &profile) {
 const bool probe=checkKernelArgument("-wexvaprobe");
 const bool fixVA=!probe && checkKernelArgument("-wexvafix"),fixMTL=!probe && checkKernelArgument("-wexleakfix");
 const bool guard=!probe && checkKernelArgument("-wexnullguard");
 if(!probe && !fixVA && !fixMTL && !guard)return;
 if(!gpu || getKernelVersion()!=KernelVersion::Sequoia || profile.overridden() || !XELP::same(profile,XELP::identify(0x9A498086,0x1)))return;
 pageFixEnabled[TGLPageFix::VA]=fixVA;pageFixEnabled[TGLPageFix::XFER]=fixVA && !guard;pageFixEnabled[TGLPageFix::MTL]=fixMTL;
 pageFixEnabled[TGLPageFix::NullGuardStatus]=pageFixEnabled[TGLPageFix::NullGuardPrepare]=pageFixEnabled[TGLPageFix::NullGuardRender]=guard;
 KernelPatcher::RouteRequest route {"_cs_validate_page",validatePage,orgValidatePage};
 pageFixReady=patcher.routeMultipleLong(KernelPatcher::KernelID,&route,1);
 patcher.clearError();
 if(auto live=OSSerializer::forTarget(nullptr,pageFixSerialize)){gpu->setProperty("TGLPageFixLive",live);live->release();}
}
}
void XeHost::init() {
 callbackHost=this;
 PE_parse_boot_argn("gfxrst",&resetFramebuffer,sizeof(resetFramebuffer));
 if(resetFramebuffer>=FB_TOTAL)resetFramebuffer=FB_DETECT;
 lilu.onPatcherLoadForce([](void *u,KernelPatcher &p){static_cast<XeHost *>(u)->processKernel(p);},this);
 lilu.onKextLoadForce(nullptr,0,[](void *u,KernelPatcher &p,size_t i,mach_vm_address_t a,size_t s){static_cast<XeHost *>(u)->processKext(p,i,a,s);},this);
 if(resetFramebuffer!=FB_NONE)lilu.onKextLoadForce(&graphics);
 lilu.onKextLoadForce(&backlight);
 xe.init();
 XeDataAccess::init();
}
void XeHost::processKernel(KernelPatcher &patcher) {
 auto info=DeviceInfo::create();if(!info)return;
 auto gpu=OSDynamicCast(IOPCIDevice,info->videoBuiltin);
 // Match WEG ordering: PCI config reads require a published IOPCIDevice.
 const bool published=gpu && WIOKit::awaitPublishing(gpu);
 const auto profile=published ? XELP::deviceProfile(gpu) : XELP::Profile {};
 // The host must dispatch framebuffer patches and accelerator rejection
 // hooks even when the selected profile deliberately has no Render backend.
 active=profile.displayAdmitted();
 preparePageFix(patcher,published ? gpu : nullptr,profile);
 const auto display=XELP::displayAdapter(profile.family);
 backlightEnabled=active && (display==XELP::DisplayAdapter::TGL || display==XELP::DisplayAdapter::ADLS ||
  display==XELP::DisplayAdapter::ADLP || display==XELP::DisplayAdapter::ADLN) &&
  info->firmwareVendor!=DeviceInfo::FirmwareVendor::Apple;
 backlightGPU=backlightEnabled ? gpu : nullptr;
 if(published) {
  uint32_t selection[] {1,profile.identity,profile.revision,profile.effectiveIdentity,
   static_cast<uint32_t>(profile.family),static_cast<uint32_t>(profile.display),
   static_cast<uint32_t>(profile.gt),active};
  gpu->setProperty("WhateverXeDeviceSelection",selection,sizeof(selection));
  if(!gpu->getProperty("model"))if(const auto name=XELP::model(profile))gpu->setProperty("model",const_cast<char *>(name),strlen(name)+1);
 }
 // Capture the unmodified firmware/GT state before admitting a new backend.
 if(!active && (profile.family==XELP::Family::AlderLakeS ||
                profile.family==XELP::Family::AlderLakeP))xe.processKernel(patcher,info);
 if(active) {
  XeDataAccess::processKernel(patcher);
  // Xe admission reads physical PCI identity. Do not install WEG's ID spoof hooks.
  // WEG normalizes the ME device discovered by Lilu for Apple's name matching.
  if(auto mei=info->managementEngine) {
   const auto name=mei->getName();
   if(!name || strcmp(name,"IMEI"))WIOKit::renameDevice(mei,"IMEI");
  }
  if(info->firmwareVendor!=DeviceInfo::FirmwareVendor::Apple) {
   if(strcmp(gpu->getName(),"IGPU"))WIOKit::renameDevice(gpu,"IGPU");
   if(!gpu->getProperty("built-in")){uint8_t value=0;gpu->setProperty("built-in",&value,1);}
   if(resetFramebuffer==FB_DETECT && !info->reportedFramebufferIsConnectorLess)resetFramebuffer=FB_COPY;
  }
  gpu->setProperty("WhateverXePhysicalGGC",gpu->configRead16(0x50),32);
  xe.processKernel(patcher,info);
  if(resetFramebuffer==FB_COPY || resetFramebuffer==FB_ZEROFILL) {
   auto v=reinterpret_cast<vc_info *>(patcher.solveSymbol(KernelPatcher::KernelID,"_vinfo"));
   if(v){consoleVinfo=*v;gotConsoleVinfo=true;}else patcher.clearError();
  }
 }
 if(!active || resetFramebuffer==FB_DETECT){resetFramebuffer=FB_NONE;graphics.switchOff();}
 DeviceInfo::deleter(info);
}
void XeHost::processKext(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
 if(!active)return;
 if(backlight.loadIndex==index) {
  if(backlightEnabled) {
   KernelPatcher::RouteRequest route("__ZN15AppleIntelPanel10setDisplayEP9IODisplay",wrapPanelSetDisplay,orgPanelSetDisplay);
   const bool ready=patcher.routeMultiple(index,&route,1,address,size);
   wexDiagRoot()->setProperty("WhateverXeBacklightProfileReady",ready);
   patcher.clearError();
  }
  return;
 }
 if(XeDataAccess::processKext(patcher,index,address,size))return;
 if(graphics.loadIndex==index) {
  gIOFBVerboseBootPtr=patcher.solveSymbol<uint8_t *>(index,"__ZL16gIOFBVerboseBoot",address,size);
  if(gIOFBVerboseBootPtr){KernelPatcher::RouteRequest route("__ZN13IOFramebuffer6initFBEv",wrapFramebufferInit,orgFramebufferInit);patcher.routeMultiple(index,&route,1,address,size);}
  patcher.clearError();return;
 }
 xe.processKext(patcher,index,address,size);
}
bool XeHost::wrapPanelSetDisplay(IOService *panel,IOService *display) {
 // Add only the missing UID-19 profile for an actual internal 16-bit PWM
 // display. Keep Apple's/custom profiles and the native selection algorithm.
 // No string patch, product-ID list, ACPI path or model-specific table.
 auto provider=panel ? panel->getProvider() : nullptr;
 auto uid=provider ? OSDynamicCast(OSString,provider->getProperty("_UID")) : nullptr;
 auto product=display ? OSDynamicCast(OSNumber,display->getProperty("DisplayProductID")) : nullptr;
 bool owned=false;
 IOFramebuffer *framebuffer=nullptr;
 for(auto entry=display;entry;entry=entry->getProvider()) {
  // IOGraphicsFamily is loaded later than this Lilu plugin. A direct
  // IOFramebuffer::metaClass import is not available in our dependency set.
  if(!framebuffer)framebuffer=static_cast<IOFramebuffer *>(entry->metaCast("IOFramebuffer"));
  if(entry==callbackHost->backlightGPU){owned=true;break;}
 }
 // setDisplay precedes publication of IODisplayParameters. Query the same
 // three-word (current,min,max) attribute as AppleIntelPanelA itself.
 uintptr_t pwm[3] {};
 const bool internal=display && display->metaCast("AppleBacklightDisplay");
 const bool range=owned && internal && framebuffer &&
  framebuffer->getAttributeForConnection(0,'bklt',pwm)==kIOReturnSuccess;
 OSDictionary *heldProfiles=nullptr;
 if(range && uid && product && pwm[1]<=UINT32_MAX && pwm[2]<=UINT32_MAX &&
    XeBacklightProfile::eligible(uid->isEqualTo("19") ? 19 : 0,pwm[1],pwm[2],internal)) {
  auto original=OSDynamicCast(OSDictionary,panel->getProperty("ApplePanels"));
  if(original && !panel->getProperty("ApplePanel")) {
   char key[32];snprintf(key,sizeof(key),"F19T%04x",product->unsigned32BitValue());
   if(!original->getObject(key) && !original->getObject("F19")) {
    auto copy=OSDictionary::withDictionary(original);
    auto curve=OSData::withBytes(XeBacklightProfile::curve,sizeof(XeBacklightProfile::curve));
    if(copy && curve && copy->setObject(key,curve) && panel->setProperty("ApplePanels",copy)) {
     // Native removes ApplePanels before retaining its selected OSData in
     // ApplePanel. Keep the candidate dictionary alive across that handoff.
     heldProfiles=copy;
     panel->setProperty("WhateverXeBacklightProfile",key);
    }
    if(curve)curve->release();
    if(copy && !heldProfiles)copy->release();
   }
  }
 }
 uint32_t inputs[]={1,owned,uid && uid->isEqualTo("19"),range ? static_cast<uint32_t>(pwm[1]) : ~0U,
  range ? static_cast<uint32_t>(pwm[2]) : 0,0};
 const bool result=FunctionCast(wrapPanelSetDisplay,callbackHost->orgPanelSetDisplay)(panel,display);
 if(heldProfiles)heldProfiles->release();
 if(panel) {
  inputs[5]=result;
  panel->setProperty("WhateverXeBacklightMatch",inputs,sizeof(inputs));
 }
 return result;
}
void XeHost::wrapFramebufferInit(IOFramebuffer *fb) {
	bool backCopy = callbackHost->gotConsoleVinfo && callbackHost->resetFramebuffer == FB_COPY;
	bool zeroFill  = callbackHost->gotConsoleVinfo && callbackHost->resetFramebuffer == FB_ZEROFILL;
	auto &info = callbackHost->consoleVinfo;

	// Copy back usually happens in a separate call to frameBufferInit
	// Furthermore, v_baseaddr may not be available on subsequent calls, so we have to copy
	if (backCopy && info.v_baseaddr) {
		// Note, this buffer is left allocated and never freed, yet there actually is no way to free it.
		callbackHost->consoleBuffer = Buffer::create<uint8_t>(info.v_rowbytes * info.v_height);
		if (callbackHost->consoleBuffer)
			lilu_os_memcpy(callbackHost->consoleBuffer, reinterpret_cast<uint8_t *>(info.v_baseaddr), info.v_rowbytes * info.v_height);
		else
			SYSLOG("wex", "console buffer allocation failure");
		// Even if we may succeed next time, it will be unreasonably dangerous
		info.v_baseaddr = 0;
	}

	uint8_t verboseBoot = *callbackHost->gIOFBVerboseBootPtr;
	// For back copy we need a console buffer and no verbose
	backCopy = backCopy && callbackHost->consoleBuffer && !verboseBoot;

	// Now check if the resolution and parameters match
	if (backCopy || zeroFill) {
		IODisplayModeID mode;
		IOIndex depth;
		IOPixelInformation pixelInfo;

		if (fb->getCurrentDisplayMode(&mode, &depth) == kIOReturnSuccess &&
			fb->getPixelInformation(mode, depth, kIOFBSystemAperture, &pixelInfo) == kIOReturnSuccess) {
			DBGLOG("wex", "fb info 1: %d:%d %u:%u:%u",
				   mode, depth, pixelInfo.bytesPerRow, pixelInfo.bytesPerPlane, pixelInfo.bitsPerPixel);
			DBGLOG("wex", "fb info 2: %u:%u %s %u:%u:%u",
				   pixelInfo.componentCount, pixelInfo.bitsPerComponent, pixelInfo.pixelFormat, pixelInfo.flags, pixelInfo.activeWidth, pixelInfo.activeHeight);

			if (info.v_rowbytes != pixelInfo.bytesPerRow || info.v_width != pixelInfo.activeWidth ||
				info.v_height != pixelInfo.activeHeight || info.v_depth != pixelInfo.bitsPerPixel) {
				backCopy = zeroFill = false;
				DBGLOG("wex", "this display has different mode");
			}
		} else {
			DBGLOG("wex", "failed to obtain display mode");
			backCopy = zeroFill = false;
		}
	}

	// For whatever reason not resetting Intel framebuffer (back copy mode) twice works better.
	if (!backCopy) *callbackHost->gIOFBVerboseBootPtr = 1;
	FunctionCast(wrapFramebufferInit, callbackHost->orgFramebufferInit)(fb);
	if (!backCopy) *callbackHost->gIOFBVerboseBootPtr = verboseBoot;

	// Finish the framebuffer initialisation by filling with black or copying the image back.
	if (FramebufferViewer::getVramMap(fb)) {
		auto src = reinterpret_cast<uint8_t *>(callbackHost->consoleBuffer);
		auto dst = reinterpret_cast<uint8_t *>(FramebufferViewer::getVramMap(fb)->getVirtualAddress());
		if (backCopy) {
			DBGLOG("wex", "attempting to copy...");
			// Here you can actually draw at your will, but looks like only on Intel.
			// On AMD you technically can draw too, but it happens for a very short while, and is not worth it.
			lilu_os_memcpy(dst, src, info.v_rowbytes * info.v_height);
		} else if (zeroFill) {
			// On AMD we do a zero-fill to ensure no visual glitches.
			DBGLOG("wex", "doing zero-fill...");
			memset(dst, 0, info.v_rowbytes * info.v_height);
		}
	}
}

