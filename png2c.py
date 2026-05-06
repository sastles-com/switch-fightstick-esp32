#!/bin/python

import sys, getopt
from PIL import Image


def build_source(data, invert_colormap, target):
  lines = ["#include <stdint.h>"]
  if target == "avr":
    lines.append("#include <avr/pgmspace.h>")
    declaration = "const uint8_t image_data[0x12c1] PROGMEM = {"
  else:
    declaration = "const uint8_t image_data[0x12c1] = {"

  lines.append("")
  lines.append(declaration)

  bytes_out = []
  for i in range(0, 4800):
    val = 0
    for j in range(0, 8):
      val |= data[(i * 8) + j] << j

    if invert_colormap:
      val = ~val & 0xFF
    else:
      val = val & 0xFF

    bytes_out.append(hex(val))

  bytes_out.append("0x0")

  for start in range(0, len(bytes_out), 16):
    lines.append("  " + ", ".join(bytes_out[start:start + 16]) + ",")

  lines.append("};")
  lines.append("")
  return "\n".join(lines)

def main(argv):
  opts, args = getopt.getopt(argv, "pshio:t:", ["output=", "target="])
  previewBilevel = False
  saveBilevel = False
  invertColormap = False
  outputPath = "image.c"
  target = "avr"

  for opt, arg in opts:
    if opt == '-h':
      usage()
      sys.exit()
    elif opt == '-p':
      previewBilevel = True
    elif opt == '-s':
      saveBilevel = True
    elif opt == '-i':
      invertColormap = True
    elif opt in ('-o', '--output'):
      outputPath = arg
    elif opt in ('-t', '--target'):
      target = arg.lower()

  if target not in ("avr", "esp32", "generic"):
    print("ERROR: target must be one of avr, esp32, generic")
    sys.exit(1)

  if len(args) != 1:
    usage()
    sys.exit(1)

  im = Image.open(args[0])                # import 320x120 png
  if not (im.size[0] == 320 and im.size[1] == 120):
    print("ERROR: Image must be 320px by 120px!")
    sys.exit(1)

  im = im.convert("1")                    # convert to bilevel image
                                          # dithering if necessary
  if previewBilevel:
    im.show()
  if saveBilevel:
    im.save("bilevel_" + args[0])
    print("Bilevel version of " + args[0] + " saved as bilevel_" + args[0])
  if not (previewBilevel or saveBilevel):
    im_px = im.load()
    data = []
    for i in range(0,120):                # iterate over the columns
      for j in range(0,320):              # and convert 255 vals to 0 to match logic in Joystick.c and invertColormap option
         data.append(0 if im_px[j,i] == 255 else 1)

    str_out = build_source(data, invertColormap, target)

    with open(outputPath, 'w') as f:       # save output into image.c
      f.write(str_out)

    if (invertColormap):
       print("{} converted with inverted colormap and saved to {}".format(args[0], outputPath))
    else:
       print("{} converted with original colormap and saved to {}".format(args[0], outputPath))

def usage():
  print("To convert to image.c: png2c.py <yourImage.png>")
  print("To convert to an inverted image.c: png2c.py -i <yourImage.png>")
  print("To preview bilevel image: png2c.py -p <yourImage.png>")
  print("To save bilevel image: png2c.py -s <yourImage.png>")
  print("To generate ESP32-friendly output: png2c.py -t esp32 <yourImage.png>")
  print("To write to another file: png2c.py -o path/to/image.c <yourImage.png>")

if __name__ == "__main__":
  if len(sys.argv[1:]) == 0:
    usage()
    sys.exit(1)
  else:
    main(sys.argv[1:])
