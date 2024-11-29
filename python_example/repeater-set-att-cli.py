#!/usr/bin/env python

import argparse
import usb

TELEORBIT_VENDOR_ID = 0x27ae
PRODUCT_ID = 0x1028


def int_to_array(d, length):
    a = []
    for i in range(length-1, -1, -1):
        a.append((d >> 8*i) & 0xff)
    return a


def write_value(request, value, index, length, widget_func, function):
    reqType = usb.TYPE_VENDOR
    data = function(widget_func, length)
    return dev.ctrl_transfer(reqType, request, value, index, data)


def set_up_out_amp(output, amp):
    buf8 = (amp | 1 << 4) if (amp > 31) else amp
    return write_value(0x0E, 0x1E, output + 1, 0x01, buf8, int_to_array)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Control the Upconverter output amplifier. Attenuation value for both output channel has to be between (0-47)')
    parser.add_argument("att1", type=int,
                        help='Attenuation value for output channel 0 (0-47).')
    parser.add_argument("att2", type=int,
                        help='Attenuation value for output channel 1 (0-47). If not specified, att1 will be used.')

    args = parser.parse_args()
    if not (0 <= args.att1 <= 47):
        parser.error("Attenuation value for output channel 0 must be between 0 and 47.")

    # Set att2 to att1 if not provided
    att2 = args.att2 if args.att2 is not None else args.att1

    # Validate att2
    if not (0 <= att2 <= 47):
        parser.error("Attenuation value for output channel 1 must be between 0 and 47.")

    dev = usb.core.find(idVendor=TELEORBIT_VENDOR_ID, idProduct=PRODUCT_ID)

    # Set attenuation for output channel 0
    status1 = set_up_out_amp(0, args.att1)
    print(f"Set output 0 to {args.att1} dB, status: {status1}")

    # Set attenuation for output channel 1
    status2 = set_up_out_amp(1, att2)
    print(f"Set output 1 to {att2} dB, status: {status2}")
