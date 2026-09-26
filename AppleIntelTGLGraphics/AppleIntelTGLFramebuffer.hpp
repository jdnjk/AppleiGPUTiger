//
//  AppleIntelTGLFramebuffer.hpp
//  AppleIntelTGLGraphics
//
//  Milestone M1.5 — basic display, no acceleration.
//
//  Claims the firmware (UEFI GOP / boot) linear framebuffer exposed in the
//  DeviceTree at /chosen/framebuffer and publishes it as a standard
//  IOFramebuffer. WindowServer composites in software on top of it, so the
//  desktop and basic windows work without any GPU command submission.
//
//  The Xe engine is NOT touched in this milestone; scanout keeps running
//  from the firmware-programmed display pipe.
//

#ifndef AppleIntelTGLFramebuffer_hpp
#define AppleIntelTGLFramebuffer_hpp

#include <IOKit/graphics/IOFramebuffer.h>

#define super IOFramebuffer

#ifdef DEBUG
#define TRACELOG kprintf
#else
#define TRACELOG IOLog
#endif

#define FBLOG(format, args...) do { TRACELOG("AppleIntelTGLFramebuffer: " format "\n", ##args); } while (false)

class AppleIntelTGLFramebuffer : public IOFramebuffer {
    OSDeclareDefaultStructors(AppleIntelTGLFramebuffer)

    // We expose exactly one mode: whatever resolution the firmware programmed
    static constexpr IODisplayModeID kFBModeID = 1;

    UInt64      fbPhysical  = 0;
    UInt32      fbWidth     = 0;
    UInt32      fbHeight    = 0;
    UInt32      fbPitch     = 0;
    UInt32      fbBpp       = 0;
    IODeviceMemory* fbRange = nullptr;

public:
    virtual bool start(IOService* provider) override;
    virtual void stop(IOService* provider) override;
    virtual void free() override;

    // --- IOFramebuffer pure virtuals (must implement) ---
    virtual IOItemCount getDisplayModeCount() override;
    virtual IOReturn getDisplayModes(IODisplayModeID* allDisplayModes) override;
    virtual IOReturn getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation* info) override;
    virtual IOReturn getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth) override;
    virtual const char* getPixelFormats() override;
    virtual UInt64 getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth) override;
    virtual IOReturn getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation* pixelInfo) override;
    virtual IODeviceMemory* getApertureRange(IOPixelAperture aperture) override;

    // --- important non-pure overrides ---
    virtual IOReturn getStartupDisplayMode(IODisplayModeID* displayMode, IOIndex* depth) override;
    virtual IOReturn setDisplayMode(IODisplayModeID displayMode, IOIndex depth) override;
    virtual IOReturn connectFlags(IOIndex connectIndex, IODisplayModeID displayMode,
            IOOptionBits* flags) override;

private:
    // -tglnofb boot arg / tgl-fb-off property (see README)
    static constexpr const char* bootargFBOff = "-tglnofb";
    static constexpr const char* propertyFBOff = "tgl-fb-off";

    bool readBootFramebufferInfo();
};

#endif /* AppleIntelTGLFramebuffer_hpp */
