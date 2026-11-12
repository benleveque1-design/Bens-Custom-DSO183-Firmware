/* Front-panel ladder decoding (pure logic, no HAL dependency).
 *
 * The DSO183 reads the three slide switches (CPL = SW1, SEN1 = SW2, SEN2 = SW3) from
 * an ADC1 resistor ladder.  Each raw ADC reading is mapped to {0,1,2} and the
 * sensitivity combination is 4 + 3*SEN1 + SEN2 (nine combinations, 4..12).
 *
 * Evidence: analysis/m1_frontpanel_storage.md (F-01) and the vendor switch reader
 * around 0x0800334c - thresholds 0x600 / 0xA00, combo = 4 + 3*SEN1 + SEN2,
 * coupling = 2 - f(CPL).
 */
#ifndef LADDER_H
#define LADDER_H

#include <stdint.h>

/* Raw ADC thresholds used by the vendor firmware. */
#define LADDER_THRESH_LO 0x600u
#define LADDER_THRESH_HI 0xA00u

/* Number of sensitivity combinations (4..12). */
#define LADDER_COMBO_MIN 4
#define LADDER_COMBO_MAX 12
#define LADDER_COMBO_COUNT (LADDER_COMBO_MAX - LADDER_COMBO_MIN + 1)

/* Map one raw ladder reading to {0,1,2}. */
uint8_t ladder_map(uint16_t raw);

/* Combine two switch positions into the sensitivity combination (4..12).
 * Returns 0 for an out-of-range position (defensive: the vendor clamps instead). */
uint8_t ladder_combo(uint8_t sen1, uint8_t sen2);

/* Coupling from the CPL ladder position: the vendor inverts the mapping. */
uint8_t ladder_coupling(uint8_t cpl_pos);

/* Index into the 9-entry calibration table for a combination (0..8).
 * Returns -1 for an invalid combination. */
int ladder_cal_index(uint8_t combo);

/* ---------------------------------------------------------------------
 * Channel mapping (vendor-derived, confirmed against the vendor's own ADC1
 * injected sequence).
 *
 * The vendor programs ADC1_JSQR = 0x002394C0 at ADC init (vendor image
 * 0x08005924).  Decoded field by field:
 *   JL   = 2   -> THREE injected conversions
 *   JSQ2 = 6   JSQ3 = 5   JSQ4 = 7   (JSQ1 = 0 is not part of the sequence)
 *
 * On the F1 the injected ranks do NOT start at JSQ1: with three conversions
 * the ST HAL encodes rank n into JSQ(n+1) (ADC_JSQR_RK_JL).  So
 *   rank 1 -> JSQ2 = ch6, rank 2 -> JSQ3 = ch5, rank 3 -> JSQ4 = ch7,
 * and the vendor switch reader reads JDR1/2/3 (+0x3C/+0x40/+0x44) as
 * SEN1/SEN2/CPL:
 *   SEN1 = ch6 / PA6,  SEN2 = ch5 / PA5,  CPL = ch7 / PA7.
 * The vendor then derives
 *   combo = 4 + 3*code(SEN1) + code(SEN2), coupling = 2 - code(CPL).
 *
 * The ladder nodes are ordinary analog nodes, so a REGULAR conversion of
 * those channels samples the same voltages.  This firmware reads them with
 * the regular group during acquisition pause: the injected group on this
 * clone sets JEOC but leaves JDR1..3 at zero.
 * --------------------------------------------------------------------- */
#define LADDER_CH_SEN1 6U   /* PA6 / ADC_IN6 - SEN1 (JDR1) */
#define LADDER_CH_SEN2 5U   /* PA5 / ADC_IN5 - SEN2 (JDR2) */
#define LADDER_CH_CPL  7U   /* PA7 / ADC_IN7 - CPL  (JDR3) */

/* Raw reading that means "no reading" (the input reader's failure value). */
#define LADDER_RAW_INVALID 0xFFFFU

/* Positions of the three ladder inputs (SEN1, SEN2, CPL).
 * Each pos is 0..2, or 3 when the reading is missing/invalid.
 * raw must hold three entries. */
void ladder_positions(const uint16_t raw[3], uint8_t pos[3]);

/* Full ladder decode from the three raw readings.
 * Returns 1 on success (combo 4..12, coupling 0..2 written), 0 when any
 * reading is invalid (combo 0, coupling 0xFF written). */
uint8_t ladder_decode(uint16_t sen1_raw, uint16_t sen2_raw, uint16_t cpl_raw,
                       uint8_t *combo_out, uint8_t *coupling_out);

#endif /* LADDER_H */
