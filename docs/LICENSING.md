# Licensing — DSO183 custom firmware

**DSO183 custom firmware — third-party licenses, uncertainties, and redistribution notes**

Read this document before redistributing.

---

## 1. Custom application code

**License:** MIT (see `LICENSE`)

**Copyright:** 2026 DSO183 custom firmware contributors

**Origin:** Original code written from analysis of the JYE Tech DSO183 stock
instrument. The application code (acquisition, trigger, timebase,
measurements, calibration, UI/menu, PC link, flash store, run state) was
written from scratch and is not copied from the vendor binary.

**Uncertainty:** This project was developed by reverse-engineering the JYE Tech
DSO183 stock firmware to understand its interfaces, protocols, and behaviour.
While the application code itself is original, there is a theoretical risk
that interface/protocol knowledge gained through reverse-engineering could be
considered derivative in some jurisdictions. The original vendor binary and the
Ghidra reverse-engineering workspace are **not part of this repository** for
licensing reasons.

If you plan to redistribute, consult legal counsel about the reverse-engineering
aspect in your jurisdiction.

---

## 2. Vendored third-party components

| Component | Location | License | Copyright |
|-----------|----------|---------|-----------|
| STM32F1xx HAL | `drivers/STM32F1xx_HAL_Driver/` | ST BSD | STMicroelectronics 2017 |
| CMSIS | `drivers/CMSIS/` | ARM | ARM LIMITED 2009–2015 |
| Adafruit GFX 5×7 font | `fonts/glcdfont.c`, `fonts/ADAFRUIT_GFX_LICENSE.txt` | BSD | Adafruit Industries 2012 |

Each component retains its own copyright and license notice. The MIT license
applied to the custom application code is compatible with all three (all are
permissive BSD-style licenses), so redistributing the combined work is
permitted provided each component's own license terms are honored:

- **ST HAL:** retain the ST copyright notice and license in source
  distributions.
- **CMSIS:** retain the ARM copyright notice.
- **Adafruit GFX font:** retain the BSD copyright notice in source; reproduce
  it in documentation and/or materials provided with binary distributions.

---

## 3. What is NOT in this repository

- **Vendor binary** — the original JYE Tech DSO183 stock firmware binary is not
  included (licensing reasons).
- **Ghidra workspace** — the reverse-engineering workspace is not included
  (licensing reasons).
- **`ghidra-mcp/`** — gitignored; the reverse-engineering workspace lives
  outside the repo.

---

## 4. Redistribution checklist

Before redistributing (source or binary):

- [ ] `LICENSE` (MIT) is included for the custom application code.
- [ ] ST HAL copyright/license notice is preserved in `drivers/STM32F1xx_HAL_Driver/`.
- [ ] CMSIS copyright notice is preserved in `drivers/CMSIS/`.
- [ ] Adafruit GFX BSD notice is preserved (`fonts/ADAFRUIT_GFX_LICENSE.txt`)
  and reproduced in documentation/materials for binary distributions.
- [ ] You have considered the reverse-engineering uncertainty in §1 for your
  jurisdiction.

---

## 5. No warranty

This firmware is provided "as is" without warranty of any kind. See the
`LICENSE` file for full terms.
