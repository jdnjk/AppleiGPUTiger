//
//  TigerLakeHW.hpp
//  AppleIntelTGLGraphics
//
//  Hardware definitions for Intel Tiger Lake (Xe LP / Gen12) iGPU.
//  Register/ID references come from the Linux i915 driver
//  (drivers/gpu/drm/i915/intel_gt.c, intel_gtt.c, pci_ids.h).
//
//  IMPORTANT: only offsets that are stable across Gen9.5-Gen12 are
//  defined here. Everything else must be verified against the
//  Tiger Lake EDS / i915 source before use.
//

#ifndef TigerLakeHW_hpp
#define TigerLakeHW_hpp

#include <stdint.h>

// ---------------------------------------------------------------------------
// Supported PCI device IDs (Intel vendor 0x8086)
// Source: i915 pci_ids.h / TGL platform IDs
// ---------------------------------------------------------------------------
struct TGLDeviceID {
    uint16_t deviceId;
    const char* name;
};

static constexpr TGLDeviceID kTGLSupportedIDs[] = {
    { 0x9A40, "Tiger Lake-LP GT2 (TGL-U 24EU)" },
    { 0x9A49, "Tiger Lake-LP GT2 (TGL-U, Iris Xe)" }, // <-- this machine: i5-1135G7
    { 0x9A60, "Tiger Lake-H GT2 (TGL-H 32EU)" },
    { 0x9A68, "Tiger Lake-H GT2 (TGL-H, Iris Xe)" },
    { 0x9A70, "Tiger Lake-H GT2 (TGL-H, Iris Xe)" },
    { 0x9A78, "Tiger Lake-H GT2 (TGL-H, Iris Xe)" },
    { 0x9AC0, "Rocket Lake-S GT1 (RKL, 32EU)" },
    { 0x9AC9, "Rocket Lake-S GT1 (RKL, 32EU)" },
    { 0x9AD9, "Rocket Lake-S GT1 (RKL, 32EU)" },
    { 0x9AF8, "Rocket Lake-S GT1 (RKL, 32EU)" },
};

static constexpr uint16_t kIntelVendorID = 0x8086;

// ---------------------------------------------------------------------------
// PCI config space offsets (iGPU's own config space)
// See i915 intel_gtt.c / EDS vol 2
// ---------------------------------------------------------------------------
static constexpr uint8_t kConfigGGC  = 0x50; // Graphics Control: GMS (DVMT pre-alloc) + GGMS
static constexpr uint8_t kConfigBDSM = 0xB0; // Base Data of Stolen Memory (32-bit, Gen6+)

// GGC layout (Gen11/Gen12):
//   bits [3:0]  GGMS (GMS for GTT), 2MB units when set to 1
//   bits [15:8] GMS  (DVMT pre-allocated memory), 32MB units
static constexpr uint16_t kGGC_GMS_MASK  = 0xFF00;
static constexpr uint16_t kGGC_GGMS_MASK = 0x000F;

// ---------------------------------------------------------------------------
// BARs (per TGL datasheet / i915 intel_gtt.c)
//   BAR0: 16MB MMIO   (engine registers, GuC, etc.)
//   BAR2: 2MB GTTMMADR (GFX translation table aperture + PTE space)
// ---------------------------------------------------------------------------
static constexpr uint8_t kTGLMMIOBARIndex = 0;
static constexpr uint8_t kTGLGTTBARIndex  = 2;

// ---------------------------------------------------------------------------
// Firmware blobs required for full operation (from linux-firmware.git):
//   i915/tgl_guc_*.bin    - Graphics microcontroller (command submission)
//   i915/tgl_huc_*.bin    - HuC (video decode assist)
//   i915/tgl_dmc_*.bin    - Display microcontroller (DC state transitions)
// They are NOT redistributable with this project; see Resources/firmware/.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Roadmap of unimplemented hardware blocks (match against i915 when adding):
//   - GTT / GGTT init                (i915 gt/gtt.c)
//   - GuC submission                 (i915 gt/uc/intel_guc_*.c)
//   - Display engine (DDI, DP/AUX)  (i915 display/intel_ddi.c, dp/)
//   - Power wells / DC9              (i915 display/intel_power_well.c)
//   - Interrupts (GT / display)      (i915 gt/intel_gt_irq.c)
// ---------------------------------------------------------------------------

#endif /* TigerLakeHW_hpp */
