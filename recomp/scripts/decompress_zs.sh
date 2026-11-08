#!/bin/bash

# Simple script to decompress .zs files found in romfs
# Usage: ./decompress_zs.sh <file_path>

TARGET_DIR="recompilation/inputs/decompressed_assets"
mkdir -p "$TARGET_DIR"

if [ -z "$1" ]; then
    echo "Usage: $0 <file_path>"
    exit 1
fi

FILE=$1

if [[ "$FILE" == *.zs ]]; then
    # Note: This assumes a tool like 'zstd' or a custom switch-zs tool is available in the path.
    # Since we don't have one, this is a placeholder instruction.
    echo "File $FILE is a .zs file."
    echo "Please provide a decompressor (e.g., zstd) to proceed automatically."
else
    echo "File $FILE is not a .zs file. Copying to $TARGET_DIR"
    cp "$FILE" "$TARGET_DIR/"
fi
