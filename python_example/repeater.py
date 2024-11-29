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


def set_up_conv_mod_power(nr, on):
    buf8 = 0x81 if on else 0x80  # power up/down MOD
    status = write_value(0x0E, 0x1D, nr, 0x01, buf8, int_to_array)
    val = 0x00

    val |= ((1 << 3) if on else (0 << 3))  # DAC1 LED
    if nr == 2:
        val |= ((1 << 4) if on else (0 << 4))  # DAC2 LED
    val |= (1 << 5)  # Playback LED

    status = write_value(0x10, 0x13, 0x00, 0x01, val, int_to_array)
    return status


def set_up_conv_dac_power(nr, on):
    buf32 = 0x00000002 if on else 0x00000012  # power up/down DAC
    return write_value(0x0D, 0x00, nr, 0x04, buf32, int_to_array)


def set_up_conv_mod_pll_power(nr, on):
    buf8 = 0x18 if on else 0x1C  # power up/down MOD PLL
    return write_value(0x0E, 0x0C, nr, 0x01, buf8, int_to_array)


def write_value(request, value, index, length, widget_func, function):
    reqType = usb.TYPE_VENDOR
    data = function(widget_func, length)
    return dev.ctrl_transfer(reqType, request, value, index, data)


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description='Control the repeater state.')
    parser.add_argument('--state', choices=['on', 'off'], required=True, help='Set the state of the repeater (on/off)')

    args = parser.parse_args()

    dev = usb.core.find(idVendor=TELEORBIT_VENDOR_ID, idProduct=PRODUCT_ID)

    if args.state == 'on':
        # Upconverter on
        status = set_up_conv_mod_power(1, True)
        status = set_up_conv_mod_power(2, True)
        status = set_up_conv_dac_power(1, True)
        status = set_up_conv_dac_power(2, True)
        status = set_up_conv_mod_pll_power(1, True)
        status = set_up_conv_mod_pll_power(2, True)

        # start command
        dev.ctrl_transfer(usb.TYPE_VENDOR, 0x00, 0x00, 0x00)

    elif args.state == 'off':
        # Upconverter off
        status = set_up_conv_mod_power(1, False)
        status = set_up_conv_mod_power(2, False)
        status = set_up_conv_dac_power(1, False)
        status = set_up_conv_dac_power(2, False)
        status = set_up_conv_mod_pll_power(1, False)
        status = set_up_conv_mod_pll_power(2, False)

        # stop command
        dev.ctrl_transfer(usb.TYPE_VENDOR, 0x00, 0x01, 0x00)
