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


def write_Value(request, value, index, length):
    reqType = usb.TYPE_VENDOR
    return dev.ctrl_transfer(reqType, request, value, index, 0x00)


def set_vga(rfslot, amp):
    return write_Value(0x06, amp, rfslot, 0x00)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Set VGA of the RF-Tuner.')
    parser.add_argument('amp1', type=int,
                        help='Amplifier value for RF slot 0 (70-210).')
    parser.add_argument('amp2', type=int,
                        help='Amplifier value for RF slot 1 (70-210).')

    args = parser.parse_args()

    if not (70 <= args.amp1 <= 210):
        parser.error("Amplifier value for RF slot 0 must be between 70 and 210.")
    # Set att2 to att1 if not provided
    args.amp2 = args.amp2 if args.amp2 is not None else args.amp1
    if not (70 <= args.amp2 <= 210):
        parser.error("Amplifier value for RF slot 1 must be between 70 and 210.")

    dev = usb.core.find(idVendor=TELEORBIT_VENDOR_ID, idProduct=PRODUCT_ID)

    # Change VGA of the RF-Tuner for both RF slots
    status1 = set_vga(0, args.amp1)
    status2 = set_vga(1, args.amp2)

    print(f"Set RF slot 0 to {args.amp1}, status: {status1}")
    print(f"Set RF slot 1 to {args.amp2}, status: {status2}")
