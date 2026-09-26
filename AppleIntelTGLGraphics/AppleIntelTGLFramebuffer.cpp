//
//  AppleIntelTGLFramebuffer.cpp
//  AppleIntelTGLGraphics
//
//  Milestone M1.5 — basic display via firmware framebuffer takeover.
//
//  The UEFI booter (OpenCore with OcAppleFramebufferInfo, or the firmware
//  itself) publishes /chosen/framebuffer in the DeviceTree with the linear
//  framebuffer set up by GOP. We read that, publish a single-mode
//  IOFramebuffer, and let WindowServer composite in software. The physical
//  memory is the SAME region the boot console draws into, so the transition
//  is seamless and nothing needs reprogramming.
//

#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <pexpert/pexpert.h>
#include "AppleIntelTGLFramebuffer.hpp"

OSDefineMetaClassAndStructors(AppleIntelTGLFramebuffer, IOFramebuffer)

bool AppleIntelTGLFramebuffer::readBootFramebufferInfo() {
    IORegistryEntry* chosen = IORegistryEntry::fromPath("/chosen/framebuffer", gIODTPlane);
    if (!chosen) {
        FBLOG("No /chosen/framebuffer in DeviceTree — cannot take over display");
        return false;
    }

    bool ok = false;
    do {
        OSData* addrData = OSDynamicCast(OSData, chosen->getProperty("framebuffer-physical-address"));
        OSData* widthData = OSDynamicCast(OSData, chosen->getProperty("framebuffer-width"));
        OSData* heightData = OSDynamicCast(OSData, chosen->getProperty("framebuffer-height"));
        OSData* pitchData = OSDynamicCast(OSData, chosen->getProperty("framebuffer-pitch"));
        OSData* depthData = OSDynamicCast(OSData, chosen->getProperty("framebuffer-depth"));

        if (!addrData || !widthData || !heightData || !pitchData || !depthData) {
            FBLOG("/chosen/framebuffer is missing required properties");
            break;
        }

        if (addrData->getLength() < 8 || widthData->getLength() < 4
            || heightData->getLength() < 4 || pitchData->getLength() < 4
            || depthData->getLength() < 4) {
            FBLOG("/chosen/framebuffer properties have unexpected sizes");
            break;
        }

        fbPhysical = *(reinterpret_cast<const UInt64*>(addrData->getBytesNoCopy()));
        fbWidth    = *(reinterpret_cast<const UInt32*>(widthData->getBytesNoCopy()));
        fbHeight   = *(reinterpret_cast<const UInt32*>(heightData->getBytesNoCopy()));
        fbPitch    = *(reinterpret_cast<const UInt32*>(pitchData->getBytesNoCopy()));
        fbBpp      = *(reinterpret_cast<const UInt32*>(depthData->getBytesNoCopy()));

        if (fbWidth == 0 || fbHeight == 0 || fbPitch == 0) {
            FBLOG("Boot framebuffer has degenerate geometry (%ux%u, pitch %u)", fbWidth, fbHeight, fbPitch);
            break;
        }
        if (fbBpp != 32) {
            // GOP is virtually always 32bpp BGRA; refuse anything else
            FBLOG("Boot framebuffer depth is %u bpp, only 32 supported", fbBpp);
            break;
        }

        FBLOG("Boot framebuffer: %ux%u, pitch %u, 32bpp @ phys 0x%016llx",
              fbWidth, fbHeight, fbPitch, (unsigned long long)fbPhysical);
        ok = true;
    } while (false);

    chosen->release();
    return ok;
}

bool AppleIntelTGLFramebuffer::start(IOService* provider) {
    // Disable switch: -tglnofb boot arg or tgl-fb-off device property
    uint32_t dummy = 0;
    if (PE_parse_boot_argn(bootargFBOff, &dummy, sizeof(dummy)) ||
        (provider && provider->getProperty(propertyFBOff))) {
        FBLOG("framebuffer takeover disabled by boot arg/property");
        return false;
    }

    if (!super::start(provider)) {
        return false;
    }

    if (!readBootFramebufferInfo()) {
        // Fail politely: firmware console keeps working, system boots fine
        return false;
    }

    fbRange = IODeviceMemory::withRange(
        static_cast<IOPhysicalAddress>(fbPhysical),
        static_cast<IOPhysicalLength>(fbPitch) * fbHeight);
    if (!fbRange) {
        FBLOG("Failed to create IODeviceMemory for framebuffer range");
        return false;
    }

    // Mark as the boot display so IOGraphics hands the console over to us
    setProperty("AAPL,boot-display", true);
    setProperty("TGL,Framebuffer-Type", "firmware GOP takeover (software compositing)");

    registerService();

    FBLOG("start complete — basic display active, GPU acceleration NOT available");
    return true;
}

void AppleIntelTGLFramebuffer::stop(IOService* provider) {
    FBLOG("stop");
    OSSafeReleaseNULL(fbRange);
    super::stop(provider);
}

void AppleIntelTGLFramebuffer::free() {
    OSSafeReleaseNULL(fbRange);
    super::free();
}

// ---------------------------------------------------------------------------
// IOFramebuffer pure virtuals
// ---------------------------------------------------------------------------

IOItemCount AppleIntelTGLFramebuffer::getDisplayModeCount() {
    return 1;
}

IOReturn AppleIntelTGLFramebuffer::getDisplayModes(IODisplayModeID* allDisplayModes) {
    if (!allDisplayModes) {
        return kIOReturnBadArgument;
    }
    allDisplayModes[0] = kFBModeID;
    return kIOReturnSuccess;
}

IOReturn AppleIntelTGLFramebuffer::getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation* info) {
    if (displayMode != kFBModeID || !info) {
        return kIOReturnUnsupportedMode;
    }
    memset(info, 0, sizeof(*info));
    info->nominalWidth  = fbWidth;
    info->nominalHeight = fbHeight;
    info->refreshRate   = 60 << 16;   // IOFixed1616: nominal 60 Hz
    info->maxDepthIndex = 0;
    info->flags = kDisplayModeValidFlag | kDisplayModeSafeFlag
                | kDisplayModeDefaultFlag | kDisplayModeBuiltInFlag;
    return kIOReturnSuccess;
}

IOReturn AppleIntelTGLFramebuffer::getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth) {
    if (!displayMode || !depth) {
        return kIOReturnBadArgument;
    }
    *displayMode = kFBModeID;
    *depth = 0;
    return kIOReturnSuccess;
}

const char* AppleIntelTGLFramebuffer::getPixelFormats() {
    return IO32BitDirectPixels;
}

UInt64 AppleIntelTGLFramebuffer::getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth) {
    (void)displayMode;
    (void)depth;
    return 0;   // obsolete, must exist, must return zero
}

IOReturn AppleIntelTGLFramebuffer::getPixelInformation(IODisplayModeID displayMode, IOIndex depth,
                                                       IOPixelAperture aperture, IOPixelInformation* pixelInfo) {
    if (displayMode != kFBModeID || depth != 0) {
        return kIOReturnUnsupportedMode;
    }
    if (aperture != kIOFBSystemAperture || !pixelInfo) {
        return kIOReturnBadArgument;
    }

    memset(pixelInfo, 0, sizeof(*pixelInfo));
    pixelInfo->bytesPerRow       = fbPitch;
    pixelInfo->bitsPerPixel      = 32;
    pixelInfo->pixelType         = kIORGBDirectPixels;
    pixelInfo->componentCount    = 3;
    pixelInfo->bitsPerComponent  = 8;
    // GOP framebuffer is little-endian BGRA in memory
    pixelInfo->componentMasks[0] = 0x000000FF;   // B
    pixelInfo->componentMasks[1] = 0x0000FF00;   // G
    pixelInfo->componentMasks[2] = 0x00FF0000;   // R
    strlcpy(pixelInfo->pixelFormat, IO32BitDirectPixels, sizeof(pixelInfo->pixelFormat));
    pixelInfo->activeWidth       = fbWidth;
    pixelInfo->activeHeight      = fbHeight;
    return kIOReturnSuccess;
}

IODeviceMemory* AppleIntelTGLFramebuffer::getApertureRange(IOPixelAperture aperture) {
    if (aperture != kIOFBSystemAperture || !fbRange) {
        return nullptr;
    }
    fbRange->retain();
    return fbRange;
}

// ---------------------------------------------------------------------------
// Non-pure overrides
// ---------------------------------------------------------------------------

IOReturn AppleIntelTGLFramebuffer::getStartupDisplayMode(IODisplayModeID* displayMode, IOIndex* depth) {
    return getCurrentDisplayMode(displayMode, depth);
}

IOReturn AppleIntelTGLFramebuffer::setDisplayMode(IODisplayModeID displayMode, IOIndex depth) {
    // Firmware picked the mode; mode changes are out of scope for M1.5
    if (displayMode == kFBModeID && depth == 0) {
        return kIOReturnSuccess;
    }
    return kIOReturnUnsupportedMode;
}

IOReturn AppleIntelTGLFramebuffer::connectFlags(IOIndex connectIndex, IODisplayModeID displayMode, IOOptionBits* flags) {
    if (connectIndex != 0 || displayMode != kFBModeID || !flags) {
        return kIOReturnUnsupportedMode;
    }
    *flags = kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag;
    return kIOReturnSuccess;
}
