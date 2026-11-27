#!/usr/bin/env python3
"""Build an HDR10+ (ST 2094-40) T.35 SEI payload for unit and vector tests.

The payload follows CTA-861-H p.253-254: the T.35 header is written by the
caller, this module emits application_identifier + the ST 2094-40 message.
"""
import struct


class BitWriter:
    def __init__(self):
        self.bits = []

    def put(self, value, width):
        for i in range(width - 1, -1, -1):
            self.bits.append((value >> i) & 1)
        return self

    def bytes(self):
        while len(self.bits) % 8:
            self.bits.append(0)
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            byte = 0
            for bit in self.bits[i:i + 8]:
                byte = (byte << 1) | bit
            out.append(byte)
        return bytes(out)


def hdr10plus_payload(windows=1, application_version=1,
                      targeted_max_luminance=1000 * 10000,
                      maxscl=(50000, 48000, 46000), average_maxrgb=30000,
                      percentiles=((90, 40000),), fraction_bright=250,
                      tone_mapping=True, knee=(2867, 2048),
                      anchors=(100, 200, 300), saturation_weight=4):
    """Return the application payload (after application_identifier)."""
    if windows < 1 or windows > 3:
        raise ValueError("HDR10+ supports 1..3 windows")
    w = BitWriter()
    w.put(application_version, 8)
    w.put(windows, 2)
    # Geometry for windows 2 and 3 (the first window covers the picture).
    for index in range(1, windows):
        w.put(0, 16).put(0, 16).put(100, 16).put(100, 16)
        w.put(50, 16).put(50, 16).put(0, 8)
        w.put(10, 16).put(20, 16).put(10, 16).put(0, 1)
    w.put(targeted_max_luminance, 27)
    w.put(0, 1)  # targeted_system_display_actual_peak_luminance_flag
    for index in range(windows):
        for component in maxscl:
            w.put(component + index, 17)
        w.put(average_maxrgb + index, 17)
        w.put(len(percentiles), 4)
        for percentage, value in percentiles:
            w.put(percentage, 7)
            w.put(value, 17)
        w.put(fraction_bright, 10)
    w.put(0, 1)  # mastering_display_actual_peak_luminance_flag
    for index in range(windows):
        w.put(1 if tone_mapping else 0, 1)
        if tone_mapping:
            w.put(knee[0], 12).put(knee[1], 12)
            w.put(len(anchors), 4)
            for anchor in anchors:
                w.put(anchor, 10)
        w.put(1, 1)  # color_saturation_mapping_flag
        w.put(saturation_weight, 6)
    return w.bytes()


def t35_payload(**kwargs):
    """Full SEI payload: T.35 header (country B5, Samsung, oriented 1, app 4)."""
    return b"\xb5\x00\x3c\x00\x01\x04" + hdr10plus_payload(**kwargs)


if __name__ == "__main__":
    import sys
    data = t35_payload()
    print(len(data), data.hex())
