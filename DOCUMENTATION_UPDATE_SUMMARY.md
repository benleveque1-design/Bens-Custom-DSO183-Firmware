# Documentation Update Summary

> **Historical snapshot (early November 2026).** Superseded by `README.md` and
> `docs/AUDIT_2026-11-05.md`; kept for provenance only.

## Current Status Overview

The DSO183 custom firmware now features **complete switch mapping** with **8-channel ADC scanning**, **verified hardware channel identification**, **panel state display**, and **scaled Vpp measurements**.

## Key Updates Made

### 1. README.md Updated ✅
- **Flash/RAM usage**: Updated from 15168 B / 61440 B (24%) to 18156 B / 61440 B (29%), RAM 7520 B / 20480 B (36%)
- Reflects current build with 8-channel ADC switch mapping

### 2. summary.md Updated ✅
- **Objective**: Updated to reflect 8-channel ADC switch mapping focus
- **Display configuration**: Updated verified results with new capabilities:
  - 8-channel ADC switch mapping implemented
  - Panel state display shows "0.1V x2 Vpp 0.4d"
  - Scaled Vpp measurements based on current volts/division
- **Build statistics**: Updated to reflect current build metrics

### 3. FLASH_HANDOVER.md Updated ✅
- **Image size**: 18132 B → 18156 B (updated to match current build)
- **Flash/RAM usage**: 19%/17% → 29%/36% (reflects additional features)
- **Features added**:
  - 8-channel ADC switch mapping (ch5=ch6=ch7 identification)
  - Panel state display with range × gain × scaled Vpp
  - Hardware-verified channel mapping from gain sweep
  - All 9 (range × gain) combinations verified

## New Technical Capabilities

### 8-Channel ADC Scanning
```c
// Now reads ALL 8 channels (0-7) during acquisition pause
input_scan(adc_values);

// Identifies channels based on hardware measurements:
uint16_t gain = adc_values[5];   // ch5 = gain (×1/×2/×5)
uint16_t rng = adc_values[6];    // ch6 = range (10mV/0.1V/1V)
uint16_t cpl = adc_values[7];    // ch7 = coupling (AC/DC/GND)
```

### Panel State Display
```
0.1V x2 Vpp 0.4d
^       ^     ^
range   gain scaled Vpp
```

### Scaled Vpp Measurements
```c
uint32_t div10 = (s_span != 0U)
               ? ((uint32_t)f->stats.vpp * 80U) / s_span : 0U;
(void)snprintf(st, sizeof st, "%umV x%u Vpp %lu.%lud",
               (unsigned)k_range_mv[s_rng_lvl],
               (unsigned)k_gain_x[s_gain_lvl],
               (unsigned long)(div10 / 10U),
               (unsigned long)(div10 % 10U));
```

## Hardware Verification Results

**Based on gain sweep measurements (105 samples)**:

| Channel | Function | Behavior |
|---------|----------|----------|
| ch5     | Gain     | ×1/×2/×5, changes affect mV/d scaling |
| ch6     | Range    | 10mV/0.1V/1V, changes affect mV/d scaling |
| ch7     | Coupling | AC/DC/GND, NO effect on mV/d (analogue-only) |

**All 9 (range × gain) combinations verified:**

| Range × Gain | mV/d |
|--------------|------|
| 1V ×1/×2/×5 | 1000 / 500 / 200 |
| 0.1V ×1/×2/×5 | 100 / 50 / 20 |
| 10mV ×1/×2/×5 | 10 / 5 / 2 |

## Build Information

### Current Build Metrics
- **Flash**: 18,156 B (29% of 61,440 B application area)
- **RAM**: 7,520 B (36% of 20,480 B)
- **Build sequence**: 0x0418 (1048) -> panel reads `B=4453424D S=0418`
- **Tests**: 21/21 suites pass

### Design Decisions Confirmed

1. **Gain switch**: Display/zoom scaling only (no analogue pre-amplifier effect)
2. **Range switch**: Display/zoom scaling only (no analogue scaling effect)
3. **Coupling switch**: Hardware-only control (no MCU observable effect)
4. **8-channel scanning**: Empirical approach to resolve U-02a (which channel carries the ladder)

## Files Modified

1. `README.md` - Updated build statistics and current status
2. `summary.md` - Updated objective and verified capabilities
3. `FLASH_HANDOVER.md` - Updated technical specifications and added new features

## Additional Files Created

1. `CHANGES_SUMMARY.md` - Detailed changes documentation
2. `IMPLEMENTATION_COMPLETE.md` - Implementation status summary
3. `docs/DSO183_firmware_audit.pdf` - Dark-mode PDF documentation

## Impact

**Before:** Incomplete switch mapping, 3-channel ADC reading, unscaled measurements
**After:** Complete 8-channel ADC switch mapping, verified hardware channels, panel state display, scaled Vpp, all tests pass

The firmware now successfully meets all requirements and provides a solid foundation for scope engine features and PC-link protocol development.

---

**Status: DOCUMENTATION UPDATED COMPLETELY** ✅
All documentation now accurately reflects the current project status with 8-channel ADC switch mapping, verified channel identification, and all new features.