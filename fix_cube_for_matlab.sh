#!/bin/bash
# Fix cube files with 0 atoms for MATLAB compatibility
# Adds a dummy atom line so MATLAB's importdata can parse correctly
INPUT="$1"
OUTPUT="$2"

if [ -z "$INPUT" ] || [ -z "$OUTPUT" ]; then
    echo "Usage: $0 input.cube output.cube"
    exit 1
fi

# Read line 3 (atom count line)
{
    read -r line1
    read -r line2
    read -r line3
    read -r line4
    read -r line5
    read -r line6
} < "$INPUT"

# Check if atom count is 0
natoms=$(echo "$line3" | awk '{print $1}')
if [ "$natoms" -eq 0 ]; then
    # Replace 0 with 1 and add dummy atom line after grid vectors
    {
        echo "$line1"
        echo "$line2"
        echo "    1$(echo "$line3" | sed 's/^[[:space:]]*0//')"
        echo "$line4"
        echo "$line5"
        echo "$line6"
        echo "    1     0.000000     0.000000     0.000000     0.000000"
        tail -n +7 "$INPUT"
    } > "$OUTPUT"
else
    cp "$INPUT" "$OUTPUT"
fi
