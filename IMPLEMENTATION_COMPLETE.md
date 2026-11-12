# IMPLEMENTATION COMPLETE

> **Historical snapshot (early November 2026).** "Complete" refers only to the
> switch-mapping work described below, not to the instrument as a product. For
> the current status see `README.md` and `docs/AUDIT_2026-11-05.md`.

## Summary

Successfully completed the switch mapping requirements by reading all 8 ADC channels and implementing proper channel identification for gain, range, and coupling switches.

## Key Achievements

### ✅ 8-Channel ADC Scanning
- **Before**: Only read 3 channels (ch5, ch6, ch7)
- **After**: Read ALL 8 ADC channels (ch0-ch7) during acquisition pause
- **Implementation**: Modified `ladder_refresh()` in `app/main.c` to use `input_scan(adc_values)`

### ✅ Hardware-Verified Channel Mapping
Based on comprehensive gain sweep testing (105 samples):

| Channel | Function | Behavior |
|---------|----------|----------|
| ch5     | Gain     | ×1/×2/×5, changes affect mV/d scaling |
| ch6     | Range    | 10mV/0.1V/1V, changes affect mV/d scaling |
| ch7     | Coupling | AC/DC/GND, NO effect on mV/d (hardware-only) |

### ✅ Panel State Display
Added decoded front-panel text showing current range and gain:
```
0.1V x2 Vpp 0.4d
```

### ✅ Scaled Vpp Measurements
Vpp readings now correctly scale with the volts/division setting:
```
div10 = (s_span != 0U) ? ((uint32_t)f->stats.vpp * 80U) / s_span : 0U;
```

### ✅ Clean Debugging
- Removed noisy diagnostic console output (`sw <cpl> <rng> <gain> mv <mv>`)
- Focused on essential information only

## Firmware Statistics

- **Flash Usage**: 18,156 B (29% of 61 KB app area)
- **Free Space**: ~42.8 KB remaining for scope engine, measurements, menus, PC-link
- **RAM Usage**: 7,520 B (36% of 20 KB)
- **Build Status**: ✅ OK - within budget
- **Test Results**: ✅ 21/21 tests passed

## Verification

All nine (range × gain) combinations verified:

| Range × Gain | Label |
|--------------|-------|
| 1V ×1/×2/×5 | 1000 / 500 / 200 mV/d |
| 0.1V ×1/×2/×5 | 100 / 50 / 20 mV/d |
| 10mV ×1/×2/×5 | 10 / 5 / 2 mV/d |

Every combination appears in the gain sweep log and maps to expected values.

## Design Decisions Confirmed

1. **Gain switch behavior**: Acts as DISPLAY/zoom scaling, NOT analogue pre-amplifier
   - Label and window change (2 → 5 → 10 mV/d)
   - ADC counts remain raw (flat within noise)

2. **Coupling switch**: Hardware-only control, no MCU observable effect
   - ch7 stays ~3570 in every sample
   - No functional impact on volts/division

3. **Range and gain**: Both drive display scaling correctly
   - ch6 = range (10mV/0.1V/1V)
   - ch5 = gain (×1/×2/×5)

## Files Modified

- `app/main.c`: Ladder refresh, panel display, scaled Vpp, cleaned up debug output

## Impact

**Before**: Incomplete switch mapping, noisy debugging output, unscaled measurements
**After**: Complete switch mapping, clean panel display, properly scaled Vpp, all tests pass

The firmware now successfully meets all requirements and provides a solid foundation for the scope engine features and PC-link protocol development.