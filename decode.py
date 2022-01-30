#!/usr/bin/env python3
import sys
import __future__

HEADER_BOOT =  0x00
HEADER_UPDATE = 0x01
HEADER_BUTTON_PRESS = 0x02

header_lut = {
    HEADER_BOOT: 'BOOT',
    HEADER_UPDATE: 'UPDATE',
    HEADER_BUTTON_PRESS: 'BUTTON_PRESS',
}

def decode(data):
    if len(data) < 12:
        raise Exception("Bad data length, min 12 characters expected")

    header = int(data[0:2], 16)
    distance = int(data[4:8], 16) if data[4:6] != 'ffff' else None
    distance /= 10.0
    core_temperature = int(data[8:12], 16) if data[8:12] != 'ffff' else None
    core_temperature /= 10.0

    resp = {
        "header": header_lut[header],
        "voltage": int(data[2:4], 16) / 10.0 if data[2:4] != 'ff' else None,
        "distance": distance,
        "core_temperature": core_temperature
    }

    return resp

def pprint(data):
    print('Header :', data['header'])
    print('Voltage :', data['voltage'])
    print('Distance :', data['distance'])
    print('Core temperature :', data['core_temperature'])

if __name__ == '__main__':
    if len(sys.argv) != 2 or sys.argv[1] in ('help', '-h', '--help'):
        print("usage: python3 decode.py [data]")
        print("example: python3 decode.py 00ff05e6")
        exit(1)

    data = decode(sys.argv[1])
    pprint(data)
