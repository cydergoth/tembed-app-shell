#!/usr/bin/python3
import argparse
import os
from PIL import Image, ImageOps
import struct

width = 170
height = 320
png = Image.new('RGB', (width, height))

with open('snap.raw', 'rb') as input_file:
   src = input_file.read()
stream = struct.unpack('<' + str(len(src) // 2) + "H", src)

for i, word in enumerate(stream):
    r = (word >> 11) & 0x1F
    g = (word >> 5) & 0x3F
    b = (word) & 0x1F
    png.putpixel((i % width, i // width), (r << 3, g << 2, b << 3))

png_r = png.rotate(270, expand=True)
png_m = ImageOps.mirror(png_r)
png_m.save('snap.png')
