#!/usr/bin/env python3
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: embed_binary.py INPUT OUTPUT SYMBOL")

    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    symbol = sys.argv[3]
    data = source.read_bytes()
    output.parent.mkdir(parents=True, exist_ok=True)

    with output.open("w", encoding="ascii") as f:
        f.write("#include <stddef.h>\n\n")
        f.write(f"const unsigned char {symbol}[] = {{\n")
        for at in range(0, len(data), 16):
            row = ", ".join(f"0x{byte:02x}" for byte in data[at:at + 16])
            f.write(f"    {row},\n")
        f.write("};\n")
        f.write(f"const size_t {symbol}_size = sizeof {symbol};\n")


if __name__ == "__main__":
    main()
