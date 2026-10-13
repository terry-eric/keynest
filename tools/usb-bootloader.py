"""Put the named PCB2 in ROM download mode using a 1200 baud USB touch."""
import argparse
import time
import serial
from serial.tools import list_ports

def normalize(value):
    return value.replace(':','').replace('-','').upper()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--expected-mac',required=True)
    args=parser.parse_args()
    target=normalize(args.expected_mac)
    if len(target)!=12 or any(c not in '0123456789ABCDEF' for c in target):
        parser.error('expected-mac must be a 12 digit hexadecimal MAC')
    ports=[p for p in list_ports.comports() if p.device.upper()==args.port.upper()]
    if len(ports)!=1 or ports[0].vid!=0x303a or ports[0].pid!=0x4005 or normalize(ports[0].serial_number or '')!=target:
        raise SystemExit('Target PCB2 runtime port/MAC does not match; nothing sent')
    link=serial.Serial(port=None,baudrate=115200,timeout=.1)
    link.dtr=False;link.rts=False;link.port=ports[0].device
    try:
        link.open();link.dtr=True;link.baudrate=1200
        time.sleep(.2);link.dtr=False
    finally:
        link.close()
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        boot=[p for p in list_ports.comports() if p.vid==0x303a and p.pid==0x1001
              and normalize(p.serial_number or '')==target]
        if len(boot)==1:
            print('PCB2 '+args.expected_mac+' download port: '+boot[0].device)
            print('Ready for esptool; no manual BOOT press used')
            return
        time.sleep(.2)
    raise SystemExit('Download port did not appear; check USB connection')

if __name__=='__main__':main()
