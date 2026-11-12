#!/usr/bin/env python3
"""Reset and run the DSO183 over SWD - the final step after --no-reset.

`write_image.py --no-reset` leaves the core halted with DBG_IWDG_STOP set, which
is deliberate: it is the only safe way to write page 0 first without the ~8 ms
v6 watchdog resetting the target mid-image.  This script performs the single
reset the image needs once every page is written and verified.

It writes only the two core debug registers (AIRCR, DHCSR), never flash, never
the option bytes and never the settings page.

usage:
    AUTHORIZED=1 python3 tools/flash/reset_run.py
"""
import os
import sys

DHCSR, AIRCR = 0xE000EDF0, 0xE000ED0C

AIRCR_SYSRESETREQ = 0x05FA0004        # VECTKEY | SYSRESETREQ
DHCSR_RUN         = 0xA05F0002        # C_DEBUGEN set, C_HALT clear -> run


def main():
    if os.environ.get("AUTHORIZED") != "1":
        print("ERROR: refusing to reset - set AUTHORIZED=1 to confirm")
        return 2

    from pyocd.core.helpers import ConnectHelper

    session = ConnectHelper.session_with_chosen_probe(
        target_override="cortex_m", blocking=False)
    if session is None:
        print("ERROR: no probe found")
        return 1
    try:
        probe = session.probe
        probe.open()
        probe.connect()
        from pyocd.coresight.ap import APv1Address
        mi = probe.get_memory_interface_for_ap(APv1Address(0))
        mi.write32(AIRCR, AIRCR_SYSRESETREQ)
        mi.write32(DHCSR, DHCSR_RUN)
        print("reset         : SYSRESETREQ (core released)")
        print("now check     : UART banner / live_check.py")
        return 0
    finally:
        session.close()


if __name__ == "__main__":
    sys.exit(main())
