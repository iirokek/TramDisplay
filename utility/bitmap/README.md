# Bitmap utility

1. Export the QGIS map as exactly 800x480 PNG.
2. Copy it to:
   `utility/bitmap/input/map.png`
3. Run from the project root:
   `python utility/bitmap/make_bitmap.py`
4. Check:
   `utility/bitmap/output/preview_1bit.png`
5. If the preview is correct, the generated `.bin` and `.h` files are ready.

The generated header will later be integrated into the ESP32 firmware.
Do not modify the exported map dimensions after QGIS export.

Keep the bitmap utility independent from the ESP32 firmware.
