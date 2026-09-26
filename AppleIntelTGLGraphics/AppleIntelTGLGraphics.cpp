//
//  AppleIntelTGLGraphics.cpp
//  AppleIntelTGLGraphics
//
//  Milestone M1 — PCI bring-up for Intel Xe LP (Gen12) iGPU:
//    * attach to the IOPCIDevice (0x8086:0x9A49 and friends)
//    * enable memory decode + bus mastering
//    * map BAR0 (MMIO) and BAR2 (GTTMMADR)
//    * decode DVMT stolen memory parameters (GGC / BDSM)
//    * publish identity into the IORegistry for later stages
//
//  Hardware knowledge cross-checked against Linux i915 (Tiger Lake).
//

#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <libkern/version.h>
#include "AppleIntelTGLGraphics.hpp"
#include "TigerLakeHW.hpp"

OSDefineMetaClassAndStructors(AppleIntelTGLGraphics, IOService)

// Convenience: property check on provider then self
#define checkProperty(a) (pciDevice && pciDevice->getProperty(a)) || getProperty(a)

static const char* nameForDeviceID(uint16_t deviceId) {
    for (const auto& entry : kTGLSupportedIDs) {
        if (entry.deviceId == deviceId) {
            return entry.name;
        }
    }
    return "Unknown TGL variant";
}

IOService* AppleIntelTGLGraphics::probe(IOService* provider, SInt32* score) {
    DEBUGLOG("%s: probe start", provider->getName());

    pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!pciDevice) {
        SYSTEMLOG("Provider %s is not an IOPCIDevice, exiting", provider->getName());
        return NULL;
    }

    const uint16_t vendorId = pciDevice->configRead16(kIOPCIConfigVendorID);
    const uint16_t deviceId = pciDevice->configRead16(kIOPCIConfigDeviceID);

    if (vendorId != kIntelVendorID) {
        SYSTEMLOGPROV("Not an Intel device (0x%04x), exiting", vendorId);
        return NULL;
    }

    bool supported = false;
    for (const auto& entry : kTGLSupportedIDs) {
        if (entry.deviceId == deviceId) {
            supported = true;
            break;
        }
    }
    if (!supported) {
        // Not ours — let whoever matches this device take it
        DEBUGLOGPROV("Device 0x%04x not in supported list, exiting", deviceId);
        return NULL;
    }

    if (checkKernelArgument(bootargOff) || checkProperty(propertyOff)) {
        SYSTEMLOGPROV("Disabled by argument/property, exiting");
        return NULL;
    }

    SYSTEMLOGPROV("Matched Xe LP (Gen12) iGPU: %s [0x%04x]", nameForDeviceID(deviceId), deviceId);

    return super::probe(provider, score);
}

bool AppleIntelTGLGraphics::enablePCI() {
    // Turn on memory decode and bus mastering, both required before any
    // MMIO access. IOPCIDevice keeps its own reference; no extra retain needed.
    if (pciDevice->setMemoryEnable(true) != kIOReturnSuccess) {
        SYSTEMLOGPROV("Failed to enable PCI memory decode");
        return false;
    }
    if (pciDevice->setBusMasterEnable(true) != kIOReturnSuccess) {
        SYSTEMLOGPROV("Failed to enable PCI bus mastering");
        return false;
    }
    DEBUGLOGPROV("PCI memory decode and bus mastering enabled");
    return true;
}

bool AppleIntelTGLGraphics::mapResources() {
    // BAR0: 16 MB MMIO (engine registers, GuC, etc.)
    mmioMap = pciDevice->mapDeviceMemoryWithIndex(kTGLMMIOBARIndex);
    if (!mmioMap) {
        SYSTEMLOGPROV("Failed to map BAR0 (MMIO)");
        return false;
    }
    SYSTEMLOGPROV("BAR0 MMIO mapped: 0x%016llx -> 0x%016llx (%llu bytes)",
                 mmioMap->getPhysicalAddress(), (uint64_t)mmioMap->getVirtualAddress(), mmioMap->getLength());

    // BAR2: GTTMMADR (2 MB on TGL). Some firmwares hide it; absence is not fatal
    // for M1, but GTT programming (M2) depends on it.
    gttMap = pciDevice->mapDeviceMemoryWithIndex(kTGLGTTBARIndex);
    if (gttMap) {
        SYSTEMLOGPROV("BAR2 GTTMMADR mapped (%llu bytes)", gttMap->getLength());
    } else {
        SYSTEMLOGPROV("BAR2 (GTTMMADR) not present or not mapped — GTT init will need attention");
    }

    return true;
}

void AppleIntelTGLGraphics::decodeStolenMemory() {
    // GGC (0x50): graphics control register
    //   GMS  [15:8]: DVMT pre-allocated memory, 32 MB units on Gen11/12
    //   GGMS [3:0]:  GTT stolen memory, 2 MB units (value 1 = 2 MB)
    const uint16_t ggc = pciDevice->configRead16(kConfigGGC);
    const uint32_t gmsMB  = ((ggc & kGGC_GMS_MASK) >> 8) * 32;
    const uint32_t ggmsMB = (ggc & kGGC_GGMS_MASK) * 2;

    SYSTEMLOGPROV("GGC = 0x%04x (DVMT pre-alloc: %u MB, GTT stolen: %u MB)", ggc, gmsMB, ggmsMB);

    // BDSM (0xB0): physical base of stolen memory, 32-bit, 32 MB aligned
    const uint32_t bdsm = pciDevice->configRead32(kConfigBDSM);
    SYSTEMLOGPROV("BDSM = 0x%08x (stolen memory base)", bdsm);

    // Expose for later stages (framebuffer handoff, GGTT setup)
    setProperty("TGL,Stolen-Memory-Base", bdsm, 32);
    setProperty("TGL,DVMT-Prealloc-MB", gmsMB, 32);
    setProperty("TGL,GTT-Stolen-MB", ggmsMB, 32);
}

void AppleIntelTGLGraphics::publishIdentity() {
    const uint16_t deviceId = pciDevice->configRead16(kIOPCIConfigDeviceID);
    const uint8_t  revision = pciDevice->configRead8(kIOPCIConfigRevisionID);

    setProperty("TGL,Driver-Version", versionString);
    setProperty("TGL,GPU-Name", nameForDeviceID(deviceId));
    setProperty("TGL,Device-ID", deviceId, 16);
    setProperty("TGL,Revision", revision, 8);
    setProperty("TGL,Generation", "Xe LP (Gen12)");
    setProperty("TGL,Milestone", "M1 - PCI bring-up");

    DEBUGLOGPROV("Identity published (device 0x%04x rev 0x%02x)", deviceId, revision);
}

void AppleIntelTGLGraphics::dumpMMIOHead() {
    if (!checkKernelArgument(bootargDumpMMIO) || !mmioMap) {
        return;
    }

    // Debug-only: log the first 4 registers of BAR0. On TGL the start of
    // BAR0 is a mirrored config-space view (VGA mirror is at 0x40000 on
    // Gen11+ — intentionally NOT touched here). Read-only, non-destructive.
    volatile uint32_t* regs = (volatile uint32_t*)mmioMap->getVirtualAddress();
    SYSTEMLOGPROV("MMIO head: 0x%08x 0x%08x 0x%08x 0x%08x",
                  regs[0], regs[1], regs[2], regs[3]);
}

// ---------------------------------------------------------------------------
// M2+ stubs. Each is a research item; see README.md "Roadmap" before
// implementing. Everything below logs and returns false for now.
// ---------------------------------------------------------------------------

bool AppleIntelTGLGraphics::initializeGTT() {
    // TODO(M2): program the GGTT scratch page + PTEs via GTTMMADR.
    // Reference: i915 gt/gen8_ppgtt.c, intel_gtt.c (ggtt_setup_view / scratch).
    SYSTEMLOGPROV("GTT initialization not implemented (M2)");
    return false;
}

bool AppleIntelTGLGraphics::loadGuCFirmware() {
    // TODO(M3): embed tgl_guc_*.bin as a C array (as itlwm does with its
    // firmware), verify against the HuC/GuC authentication engine, and
    // submit the first (nop) batch.
    // Reference: i915 gt/uc/intel_guc_fw.c, intel_huc_fw.c
    SYSTEMLOGPROV("GuC firmware loading not implemented (M3)");
    return false;
}

// ---------------------------------------------------------------------------
// IOService lifecycle
// ---------------------------------------------------------------------------

bool AppleIntelTGLGraphics::start(IOService* provider) {
    if (!super::start(provider)) {
        return false;
    }

    pciDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!pciDevice) {
        SYSTEMLOG("start: provider is not an IOPCIDevice");
        return false;
    }

    SYSTEMLOGPROV("start begin (%s)", versionString);

    if (!enablePCI()) {
        return false;
    }

    if (!mapResources()) {
        return false;
    }

    decodeStolenMemory();
    publishIdentity();
    dumpMMIOHead();

    // Attempt next milestones; failure is expected and non-fatal for now.
    initializeGTT();
    loadGuCFirmware();

    // Make ourselves visible in the IORegistry:
    //   ioreg -l | grep -i tgl
    registerService();

    SYSTEMLOGPROV("start complete — M1 bring-up OK, acceleration NOT available yet");
    return true;
}

void AppleIntelTGLGraphics::stop(IOService* provider) {
    SYSTEMLOGPROV("stop");
    OSSafeReleaseNULL(gttMap);
    OSSafeReleaseNULL(mmioMap);
    super::stop(provider);
}

void AppleIntelTGLGraphics::free() {
    // Defensive: stop() should already have released these
    OSSafeReleaseNULL(gttMap);
    OSSafeReleaseNULL(mmioMap);
    super::free();
}
