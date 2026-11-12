#!/usr/bin/env python3
"""Small host control/query tool for DSO183 over the PC link."""
import argparse, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import remote_proto as rp
from remote_client import LinkClient, SerialLink, find_port

GETV = {k:v for k,v in rp.__dict__.items() if k.startswith('GETV_')}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default=None)
    ap.add_argument('cmd', nargs='?', default='status',
                    help='status|run|stop|stream-on/off|tb N|trig N|mode auto|norm|single|slope rise|fall|auto|'
                         'cal-offset-mv N|cal-gain-x1000 N|cal-save|cal-reset|wave-save 0|1|wave-recall 0|1|avg 1|2|4|8|meas|get SUB')
    ap.add_argument('arg', nargs='?', default=None)
    args = ap.parse_args()
    port = find_port(args.port)
    s = SerialLink(port); c = LinkClient(s, timeout=2.0)
    try:
        s.drain(0.15)
        cmd = args.cmd.lower().replace('_','-')
        if cmd == 'status':
            st,v = c.cmd(rp.OP_GET_STATUS,0); print('status',st,hex(v),'run',v&1,'stream',(v>>1)&1)
        elif cmd == 'run': print(c.cmd(rp.OP_RUN,1))
        elif cmd == 'stop': print(c.cmd(rp.OP_STOP,0))
        elif cmd == 'stream-on': print(c.cmd(rp.OP_STREAM,1))
        elif cmd == 'stream-off': print(c.cmd(rp.OP_STREAM,0))
        elif cmd == 'tb': print(c.cmd(rp.OP_SET_TIMEBASE,int(args.arg)))
        elif cmd == 'trig': print(c.cmd(rp.OP_SET_TRIGGER,int(args.arg)))
        elif cmd == 'mode':
            m = {'auto':0,'norm':1,'normal':1,'single':2}[args.arg.lower()]
            print(c.cmd(rp.OP_SET_TRIG_MODE,m))
        elif cmd == 'slope':
            m = {'rise':0,'rising':0,'fall':1,'falling':1,'auto':2}[args.arg.lower()]
            print(c.cmd(rp.OP_SET_TRIG_SLOPE,m))
        elif cmd == 'cal-offset-mv':
            v = int(args.arg) & 0xFFFF
            print(c.cmd(rp.OP_CAL_SET_OFFSET_MV, v))
        elif cmd == 'cal-gain-x1000':
            print(c.cmd(rp.OP_CAL_SET_GAIN_X1000, int(args.arg)))
        elif cmd == 'cal-save': print(c.cmd(rp.OP_CAL_SAVE, 0))
        elif cmd == 'cal-reset': print(c.cmd(rp.OP_CAL_RESET, 0))
        elif cmd == 'wave-save': print(c.cmd(rp.OP_WAVE_SAVE, int(args.arg)))
        elif cmd == 'wave-recall': print(c.cmd(rp.OP_WAVE_RECALL, int(args.arg)))
        elif cmd == 'avg': print(c.cmd(rp.OP_SET_AVG, int(args.arg)))
        elif cmd == 'get':
            sub = GETV.get(args.arg.upper(), None)
            if sub is None: sub = int(args.arg, 0)
            print(args.arg, c.cmd(rp.OP_GET_VALUE,sub))
        elif cmd == 'meas':
            keys = ['GETV_VPP_COUNTS','GETV_VRMS_COUNTS','GETV_MIN','GETV_MAX','GETV_FREQ_HZ','GETV_DUTY_X10',
                    'GETV_TRIG_MODE','GETV_TRIG_LEVEL','GETV_TIMEBASE','GETV_SPAN','GETV_TRIG_SLOPE','GETV_BUILD_SEQ',
                    'GETV_CAL_OFFSET_MV','GETV_CAL_GAIN_X1000','GETV_WAVE_SAVED_0','GETV_WAVE_SAVED_1','GETV_AVG_N']
            for k in keys:
                if k not in GETV: continue
                st,v = c.cmd(rp.OP_GET_VALUE, GETV[k]); print(k.replace('GETV_',''), v)
        else:
            print('unknown', cmd); return 2
    finally:
        s.ser.close()
    return 0

if __name__=='__main__': raise SystemExit(main())
