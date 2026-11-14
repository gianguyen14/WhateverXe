# Changelog

## Unreleased

### Fixed

- Add opt-in page fixes for Tiger Lake `9A49` on Sequoia, applied in memory after code-signing validation so the bundles stay untouched. `-wexvafix` stops streamed video from stalling the render engine (#81) and stops pixel transfers (scaling, colour conversion) from crashing the VA driver at +0x1336f3 (#23), `-wexleakfix` stops the Metal driver leaking 76 KB per command buffer (#47), and `-wexvaprobe` only counts the matching pages.
- Add opt-in `-wexteardownfix` for Tiger Lake: before a GPU context or task is freed, wait up to 50 ms for the scheduler to report it idle, so in-flight post-sync writes no longer fault on unmapped pages and loop GPU resets when a WebRTC call or a GPU client ends (#62). Ported from starterakun08-tech/WhateverXe@023ede1; only installed when the accelerator's `free` functions match the 16.0.32 build, and `TGLTeardownDrainLive` on the IGPU counts frees, waits and drained waits.

### Changed

- Keep acceleration on Tiger Lake GT2 parts whose fuses report an unlisted shape, such as the i5-1145G7 (5 DSS, 80 EU, 6 L3 banks). 0.7.0 withheld the accelerator for them, leaving no Metal and a black desktop; they now keep the 0.6.1 donor URB layout (`TGLGT2DonorLayout`).
- Add `-wexnohwdecode`, which makes VideoToolbox decode video in software while hardware encode stays available. AppleGVA loads one accelerator list for decode and encode, so no registry key withdraws decode alone: video contexts are refused in `VTDecoderXPCService`, and the HEVC decode keys are withdrawn. Hardware decode can hang the render engine on Tiger Lake under `-disablegfxfirmware`.
- Reset a hung Tiger Lake engine with the Gen12 `RING_RESET_CTL` handshake and a bounded `GDRST` wait under `-disablegfxfirmware`. Without GuC the native reset waited on the domain forever, freezing the display when a video decode kernel never retired. Each reset logs `TGL engine reset`; `-wexnoreset` restores the native path.
- Keep diagnostic records off the I/O Registry root. Clients that copy the root's properties, such as PerfPowerServices, serialized about 1100 records and several MB from live serializers on every call, holding a CPU core busy. Boot with `-wexdiag` to publish them on the root again.
- Scale the Tiger Lake GPU clock with load under `-disablegfxfirmware`. Without GuC SLPC the request stayed at RP0 whenever the GT was awake, taking power from the CPU on 15 W parts; it now follows RC6 residency between RP1 and RP0. `-wexrpsmax` restores the fixed RP0 request.
- Report GPU utilization with `-disablegfxfirmware` on Tiger Lake. The native statistics page is filled by graphics firmware, so Activity Monitor and `PerformanceStatistics` always showed 0%; each context's measured GPU run time is now accounted there (Render and Compute together, Blit, and media engines, the three units the native reader reports, including the running context's live time at each read, with each unit's growth bounded by elapsed time), and the frequency-scaled value uses Tiger Lake's RPSTAT1/RP_STATE_CAP so it no longer exceeds 100%.

## 0.7.0

Use the latest [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics/releases) package with this update.

### Added

- Add USB-C DisplayPort Alt Mode output on Alder Lake-class GPUs, including multi-display startup and reconnection.
- Add experimental MIPI DSI display support with graphics acceleration, brightness control, display off/on, rotation, HiDPI and external-display coexistence. Tested on GPD MicroPC 2 and CHUWI MiniBook X N100 only; do not expect it to work on other devices. System sleep/wake remains unsupported.

### Fixed

- Fix graphics initialization and acceleration on additional supported Tiger Lake and Alder/Raptor Lake GPU variants, including single-channel configurations.
- Fix graphics initialization on compatible Sequoia and Tahoe updates without requiring an exact OS build match.
- Fix display freezes during cursor resizing and improve multi-display startup and display off/on with MIPI DSI panels.
- Fix system power-off hangs with a USB-C display connected on Alder Lake-class GPUs.

## 0.6.1-sequoia.1

Unofficial prerelease based on 0.6.1. Use the latest [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics/releases) package. Hardware validation is limited to a single Tiger Lake `9A49` system on Sequoia 15.8.1.

### Changed

- Use single-pass Tiger Lake GPU recovery on Sequoia with `-disablegfxfirmware`, as on Big Sur and Tahoe, so a GPU reset returns to native scheduling instead of waiting inside the recovery work loop.
- Capture GPU hang details and the failure-only command-streamer wake on Sequoia.
- Accept Sequoia servicing kernels after Darwin 24.6.
- Keep the optional completion-cache, indirect-context-cache, same-tail restore, GSYNC-disable and full-slice RPCS overrides disabled on every macOS version.

## 0.6.1

Use the latest [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics/releases) package.

### Added

- Add experimental internal display (eDP) and brightness support for Alder Lake-N / Twin Lake and Alder Lake-H/P/U / Raptor Lake-H/P/U GPUs. Not yet tested on hardware.

### Fixed

- Fix DEBUG boot freezes and restart loops caused by excessive kernel stack usage.

## 0.6.0

Use the latest [WhateverXe Graphics](https://github.com/b00t0x/WhateverXe-Graphics/releases) package.

### Added

- Add Alder Lake-H/P/U and Raptor Lake-H/P/U GPU support with automatic resource detection and VBT-selected fixed HDMI routes.
- Add experimental fixed TC DisplayPort output with matching AUX wiring; Alder Lake-H has been tested through a DP-to-HDMI adapter.
- Support startup with multiple fixed TC displays, including supported firmware-scaled boot displays.

### Fixed

- Improve display clock ownership, output shutdown and reuse across the shared external-display paths.
- Stop display teardown and reuse when required shutdown steps fail.
- [WhateverXe Graphics 0.3.0](https://github.com/b00t0x/WhateverXe-Graphics/releases) fixes shader compilation failures on Sonoma–Tahoe by expanding supported AIR inputs.

Alder Lake-H hardware checks cover Big Sur–Tahoe. Other newly accepted mobile configurations remain untested. Internal panels and Type-C-managed output on the newly supported mobile GPUs still need implementation; existing DisplayPort reconnection and Sequoia App Store video hangs remain unresolved. See the [README](https://github.com/b00t0x/WhateverXe#tested-features-and-known-issues) for coverage and limits.

## 0.5.0

The newly added GPUs require [WhateverXe Graphics 0.2.0](https://github.com/b00t0x/WhateverXe-Graphics/releases). Its rendering fixes also benefit previously supported GPUs; updating both components is recommended.

### Added

- Add Alder Lake-S graphics acceleration and HDMI/DisplayPort output, including compatible USB-C wiring and up to three displays.
- Accept Tiger Lake-H45, Alder Lake-HX and Raptor Lake-S/HX using shared support. These configurations still need hardware testing.
- Use firmware information to configure more display layouts automatically.

### Fixed

- Avoid some startup panics caused by display hardware not being ready.
- Restore Safari graphics-driver access regardless of where macOS installs Safari updates.
- [WhateverXe Graphics 0.2.0](https://github.com/b00t0x/WhateverXe-Graphics/releases) fixes observed Chrome drawing failures and rendering errors on smaller integrated GPUs.

DisplayPort/USB-C reconnection can still freeze the computer. See the [README](https://github.com/b00t0x/WhateverXe#tested-features-and-known-issues) for tested configurations and other known issues.

## 0.4.0

Add experimental Tiger Lake laptop display support and automatic Display 13 tiling selection.

- Read connector routing from structurally valid firmware VBT tables even when their checksum is stale, while retaining bounds and connector checks.
- Add Tiger Lake internal eDP output with EDID-derived color depth, firmware-derived panel setup and brightness control with SSDT-PNLF.
- Add firmware-selected Tiger Lake USB-C DisplayPort Alt Mode output, including PHY, AUX and lane assignment handling.
- Keep eDP scrambler settings synchronized with the panel during link training to restore normal output after display sleep.
- Remove the `-wex-nopanel` boot argument: it is no longer needed because internal displays are detected automatically on laptops and mini PCs.
- Select Y-tiled scanout automatically on the supported Display 13 backend, removing the OpenCore `AAPL,GfxYTile` requirement.

Laptop display and brightness checks cover Big Sur through Tahoe. HDMI and both tested USB-C ports passed reconnection checks on Tahoe; Tiger Lake and Alder Lake-N mini PCs passed Big Sur/Tahoe regression checks. Automatic Display 13 Y tiling was verified on Alder Lake-N with Big Sur and Tahoe.

This update does not require updating or reinstalling the WhateverXe Graphics package. Fixed DisplayPort reconnection remains a known issue, and system sleep is not validated; see the [README](https://github.com/b00t0x/WhateverXe#tested-features-and-known-issues) for coverage and limitations.

## 0.3.0

Xe-LP graphics compatibility for the configurations described in the [README](https://github.com/b00t0x/WhateverXe#compatibility).

- One WEX binary supports Big Sur through Tahoe on the implemented Tiger Lake and Alder Lake-N / Twin Lake backends.
- Graphics installation uses the signed [WhateverXe Graphics pkg](https://github.com/b00t0x/WhateverXe-Graphics/releases), with no Xe System-volume patch or OCLP dependency.
- Includes cross-version OS/driver ABI adaptations, OpenCL compatibility and late semaphore mapping invalidation fixes.
- Supports signed Data graphics bundles with Mod AMFIPass, without `amfi=0x80` or `ipc_control_port_options=0`.
- Preserve debug information in the bundled dSYM before stripping the kext.
- Tiger Lake and Alder Lake-N have completed the six-OS functional checks described in the README. Known issues and hardware coverage limits still apply.

## 0.2.3

Fix DisplayPort startup and hotplug issues.

- Fix a kernel panic when booting with only DisplayPort connected.
- Fix DisplayPort detection when first connected after an HDMI-only boot.

The text-rendering fix is provided separately by the Xe root patch in [OpenCore Legacy Patcher 3.0.0-rc.3+m.2](https://github.com/b00t0x/OpenCore-Legacy-Patcher/releases/tag/3.0.0-rc.3%2Bm.2).

## 0.2.2

Xe-LP graphics compatibility for the configurations described in the [README](https://github.com/b00t0x/WhateverXe#compatibility).
