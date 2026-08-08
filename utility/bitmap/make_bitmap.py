"""Generate 1-bit map assets from an exact-size QGIS PNG export."""

from pathlib import Path
import sys

try:
    from PIL import Image
except ImportError:
    print("Pillow is required. Install it with:", file=sys.stderr)
    print("pip install pillow", file=sys.stderr)
    raise SystemExit(1)


THRESHOLD = 160

WIDTH = 800
HEIGHT = 480
EXPECTED_BYTES = WIDTH * HEIGHT // 8

UTILITY_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = UTILITY_DIR.parent.parent
INPUT_PATH = UTILITY_DIR / "input" / "map.png"
OUTPUT_DIR = UTILITY_DIR / "output"
PREVIEW_PATH = OUTPUT_DIR / "preview_1bit.png"
BINARY_PATH = OUTPUT_DIR / "tampere_map.bin"
HEADER_PATH = OUTPUT_DIR / "tampere_map.h"


def project_relative(path: Path) -> str:
    """Return a stable, forward-slash path for console output."""
    return path.relative_to(PROJECT_ROOT).as_posix()


def load_and_threshold_image() -> Image.Image:
    """Load the map, validate its geometry, and return an 8-bit B/W image."""
    if not INPUT_PATH.is_file():
        raise RuntimeError(
            f"Input image not found: {project_relative(INPUT_PATH)}\n"
            "Export the QGIS map as exactly 800x480 PNG and copy it there."
        )

    try:
        with Image.open(INPUT_PATH) as source:
            if source.size != (WIDTH, HEIGHT):
                actual_width, actual_height = source.size
                raise RuntimeError(
                    "Input image has incorrect dimensions: "
                    f"{actual_width}x{actual_height}; expected {WIDTH}x{HEIGHT}.\n"
                    "The utility will not resize, crop, or otherwise alter image geometry."
                )

            grayscale = source.convert("L")
    except RuntimeError:
        raise
    except (OSError, ValueError) as error:
        raise RuntimeError(f"Could not open {project_relative(INPUT_PATH)}: {error}") from error

    return grayscale.point(lambda value: 0 if value <= THRESHOLD else 255, mode="L")


def pack_bitmap(image: Image.Image) -> bytes:
    """Pack black pixels as 1 bits, row-major and MSB first."""
    pixels = image.tobytes()
    packed = bytearray()

    for offset in range(0, len(pixels), 8):
        packed_byte = 0
        for bit_index, pixel in enumerate(pixels[offset : offset + 8]):
            if pixel == 0:
                packed_byte |= 1 << (7 - bit_index)
        packed.append(packed_byte)

    if len(packed) != EXPECTED_BYTES:
        raise RuntimeError(
            f"Generated binary has incorrect size: {len(packed)} bytes; "
            f"expected {EXPECTED_BYTES} bytes."
        )

    return bytes(packed)


def write_header(bitmap: bytes) -> None:
    """Write the packed bitmap as a readable C++ header, 16 bytes per line."""
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"static constexpr uint16_t TAMPERE_MAP_WIDTH = {WIDTH};",
        f"static constexpr uint16_t TAMPERE_MAP_HEIGHT = {HEIGHT};",
        f"static constexpr uint32_t TAMPERE_MAP_BYTES = {EXPECTED_BYTES};",
        "",
        "static const uint8_t tampere_map[] = {",
    ]

    for offset in range(0, len(bitmap), 16):
        chunk = bitmap[offset : offset + 16]
        lines.append("    " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")

    lines.extend(["};", ""])
    HEADER_PATH.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> None:
    if not 0 <= THRESHOLD <= 255:
        raise RuntimeError(f"THRESHOLD must be between 0 and 255; got {THRESHOLD}.")

    preview = load_and_threshold_image()
    bitmap = pack_bitmap(preview)

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    preview.save(PREVIEW_PATH, format="PNG")
    BINARY_PATH.write_bytes(bitmap)
    write_header(bitmap)

    print("Bitmap generation complete")
    print()
    print(f"Input:      {project_relative(INPUT_PATH)}")
    print(f"Size:       {WIDTH}x{HEIGHT}")
    print(f"Threshold:  {THRESHOLD}")
    print(f"Binary:     {len(bitmap)} bytes")
    print()
    print("Generated:")
    print(f"  {project_relative(PREVIEW_PATH)}")
    print(f"  {project_relative(BINARY_PATH)}")
    print(f"  {project_relative(HEADER_PATH)}")


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1)
