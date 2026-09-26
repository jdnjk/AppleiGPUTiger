//
//  AppleIntelTGLGraphics.hpp
//  AppleIntelTGLGraphics
//
//  Bring-up kernel extension for the Intel Tiger Lake (Xe LP / Gen12)
//  integrated GPU, targeting the i5-1135G7's Iris Xe (8086:9A49) on
//  macOS 26 (Tahoe), where no Apple driver exists.
//
//  This driver covers milestone M1 only (PCI bring-up + resource mapping).
//  See README.md for the full roadmap.
//

#ifndef AppleIntelTGLGraphics_hpp
#define AppleIntelTGLGraphics_hpp

#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <pexpert/pexpert.h>

#define super IOService

#ifdef DEBUG
#define TRACELOG kprintf
#else
#define TRACELOG IOLog
#endif

#define SYSTEMLOG(format, args...) do { TRACELOG("AppleIntelTGLGraphics: " format "\n", ##args); } while (false)
#define SYSTEMLOGPROV(format, args...) do { TRACELOG("AppleIntelTGLGraphics: %s: " format "\n", pciDevice->getName(), ##args); } while (false)

#ifdef DEBUG
#define DEBUGLOG(format, args...) SYSTEMLOG(format, ##args)
#define DEBUGLOGPROV(format, args...) SYSTEMLOGPROV(format, ##args)
#else
#define DEBUGLOG(format, args...)
#define DEBUGLOGPROV(format, args...)
#endif

class AppleIntelTGLGraphics : public IOService {
    OSDeclareDefaultStructors(AppleIntelTGLGraphics)

    // Boot args
    static constexpr const char *bootargOff {"-tgloff"};      // Disable the kext entirely
    static constexpr const char *bootargDumpMMIO {"-tglmmiodump"}; // Log first bytes of BAR0 (debug)

    // Properties (checked on the PCI device, then on ourselves)
    static constexpr const char *propertyOff {"tgl-off"};

    static constexpr const char *versionString =
#ifdef DEBUG
    "DBG-"
#else
    "REL-"
#endif
     __DATE__ "-" __TIME__;

public:
    virtual IOService* probe(IOService* provider, SInt32* score) override;
    virtual bool start(IOService* provider) override;
    virtual void stop(IOService* provider) override;
    virtual void free() override;

private:
    IOPCIDevice* pciDevice = nullptr;

    // Mapped GPU resources (retained IOMemoryMaps, released in free())
    IOMemoryMap* mmioMap = nullptr;
    IOMemoryMap* gttMap  = nullptr;

    // M1: PCI bring-up
    bool enablePCI();
    bool mapResources();
    void publishIdentity();
    void decodeStolenMemory();

    // Debug helpers
    void dumpMMIOHead();

    // M2+ placeholders — see README.md roadmap
    bool initializeGTT();      // GGTT programming (i915 gt/gtt.c)
    bool loadGuCFirmware();    // GuC/HuC/DMuC firmware (i915 gt/uc/)

    static bool checkKernelArgument(const char* arg) {
        uint32_t value = 0;
        return PE_parse_boot_argn(arg, &value, sizeof(value));
    };
};

#endif /* AppleIntelTGLGraphics_hpp */
