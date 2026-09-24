# /// script
# requires-python = ">=3.11"
# dependencies = ["mcp>=1.2,<2", "pillow>=10"]
# ///
"""MCP zoom tool: crop a region of an image file and return it magnified.

Adapted from https://platform.claude.com/cookbook/multimodal-crop-tool.
view_image returns the image pre-resized to exactly what Claude sees, so the
pixel coordinates Claude passes to zoom map 1:1 onto it; zoom crops from the
full-resolution original and upscales the crop to fill the image budget.
"""

import math
import os
from io import BytesIO
from pathlib import Path

from mcp.server.fastmcp import FastMCP, Image
from PIL import Image as PILImage

# High-resolution tier (Claude 4.7 and later) token budget. The edge is capped at
# 2000 because Claude Code downscales larger images client-side, which would break
# the 1:1 coordinate mapping. Use 1568/1568 for standard-tier models.
MAX_EDGE = int(os.environ.get("ZOOM_MAX_EDGE", 2000))
MAX_TOKENS = int(os.environ.get("ZOOM_MAX_TOKENS", 4784))
MAX_PNG_BYTES = 1_500_000

mcp = FastMCP("zoom")


def count_image_tokens(width: int, height: int) -> int:
    return math.ceil(width / 28) * math.ceil(height / 28)


def resized_size(width: int, height: int) -> tuple[int, int]:
    """The size Claude resizes an image to (reference implementation from the vision docs)."""

    def fits(w: int, h: int) -> bool:
        return (
            math.ceil(w / 28) * 28 <= MAX_EDGE
            and math.ceil(h / 28) * 28 <= MAX_EDGE
            and count_image_tokens(w, h) <= MAX_TOKENS
        )

    if fits(width, height):
        return (width, height)
    if height > width:
        h, w = resized_size(height, width)
        return (w, h)
    aspect = width / height
    lo, hi = 1, width
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if fits(mid, max(round(mid / aspect), 1)):
            lo = mid
        else:
            hi = mid
    return (lo, max(round(lo / aspect), 1))


def load(path: str) -> PILImage.Image:
    image = PILImage.open(Path(path).expanduser().resolve())
    image.load()
    if image.mode == "RGB":
        return image
    # Composite transparency onto white; a bare convert("RGB") turns it black.
    rgba = image.convert("RGBA")
    background = PILImage.new("RGB", rgba.size, "white")
    background.paste(rgba, mask=rgba.getchannel("A"))
    return background


def view_of(image: PILImage.Image) -> PILImage.Image:
    target = resized_size(image.width, image.height)
    if image.size == target:
        return image
    return image.resize(target, PILImage.Resampling.LANCZOS)


def encode(image: PILImage.Image, prefer_png: bool) -> Image:
    if prefer_png:
        buffer = BytesIO()
        image.save(buffer, format="PNG", optimize=True)
        if buffer.tell() <= MAX_PNG_BYTES:
            return Image(data=buffer.getvalue(), format="png")
    buffer = BytesIO()
    # subsampling=0 keeps full chroma so thin colored lines don't smear.
    image.save(buffer, format="JPEG", quality=92, subsampling=0)
    return Image(data=buffer.getvalue(), format="jpeg")


@mcp.tool()
def view_image(path: str) -> list:
    """Show an image file at exactly the resolution you see it, and report that size.

    Call this before zoom: zoom coordinates are pixels in the image as returned here.
    Prefer this over reading the image file directly when you may need to zoom.

    Args:
        path: Absolute path to a PNG, JPEG, or other Pillow-readable image.
    """
    original = load(path)
    view = view_of(original)
    note = (
        f"{path}: you see it at {view.width}x{view.height}px"
        f" (original {original.width}x{original.height}px)."
        " Pass zoom coordinates in the size you see, origin top-left."
    )
    return [note, encode(view, prefer_png=True)]


@mcp.tool()
def zoom(path: str, x1: int, y1: int, x2: int, y2: int) -> list:
    """Zoom into a rectangular region of an image to see it magnified.

    Use whenever text, numbers, thin lines, or small details are too small to read
    confidently. The region is cropped from the full-resolution original and scaled
    up to fill the image budget. Zoom again on a tighter region for finer detail.

    Args:
        path: Absolute path to the image (same path given to view_image).
        x1: Left edge, in pixels of the image as view_image showed it.
        y1: Top edge, in pixels.
        x2: Right edge, in pixels (greater than x1).
        y2: Bottom edge, in pixels (greater than y1).
    """
    original = load(path)
    view_w, view_h = resized_size(original.width, original.height)
    x1, x2 = max(0, min(x1, view_w)), max(0, min(x2, view_w))
    y1, y2 = max(0, min(y1, view_h)), max(0, min(y2, view_h))
    if x1 >= x2 or y1 >= y2:
        return [f"Error: invalid region; need x1 < x2 and y1 < y2 within {view_w}x{view_h}"]
    # Width and height round independently in resized_size, so scale each axis.
    sx, sy = original.width / view_w, original.height / view_h
    box = (round(x1 * sx), round(y1 * sy), round(x2 * sx), round(y2 * sy))
    cropped = original.crop(box)
    zoomed = cropped.resize(
        resized_size(cropped.width * 10_000, cropped.height * 10_000),
        PILImage.Resampling.LANCZOS,
    )
    note = (
        f"Zoomed ({x1},{y1})-({x2},{y2}) of the {view_w}x{view_h}px view:"
        f" a {cropped.width}x{cropped.height}px original region"
        f" magnified to {zoomed.width}x{zoomed.height}px."
    )
    return [note, encode(zoomed, prefer_png=False)]


if __name__ == "__main__":
    mcp.run()
