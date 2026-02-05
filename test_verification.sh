#!/bin/bash
# Verification test for TuTraSt C++ implementation

echo "=========================================="
echo "TuTraSt C++ Implementation Verification"
echo "=========================================="
echo

# Check build system
echo "1. Checking build system..."
if [ -f "build/tutrast" ]; then
    echo "   ✓ Executable exists"
else
    echo "   ✗ Executable not found"
    exit 1
fi

# Check input files
echo "2. Checking input files..."
if [ -f "grid.cube" ] && [ -f "input.param" ]; then
    echo "   ✓ Input files present"
else
    echo "   ✗ Input files missing"
    exit 1
fi

# Run the program
echo "3. Running TuTraSt..."
timeout 120 ./build/tutrast > /tmp/tutrast_output.log 2>&1
EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ]; then
    echo "   ✓ Program completed successfully"
else
    echo "   ✗ Program failed with exit code $EXIT_CODE"
    exit 1
fi

# Check output files
echo "4. Checking output files..."
EXPECTED_FILES=(
    "basis.dat"
    "processes_1000.dat"
    "tunnel_info.out"
    "TS_data.out"
    "BT.dat"
    "Evol_1000.dat"
)

ALL_PRESENT=true
for file in "${EXPECTED_FILES[@]}"; do
    if [ -f "$file" ]; then
        echo "   ✓ $file exists"
    else
        echo "   ✗ $file missing"
        ALL_PRESENT=false
    fi
done

if [ "$ALL_PRESENT" = false ]; then
    exit 1
fi

# Check critical files have content
echo "5. Validating file content..."
if [ -s "basis.dat" ] && [ -s "tunnel_info.out" ] && [ -s "BT.dat" ]; then
    echo "   ✓ Critical output files contain data"
else
    echo "   ✗ Some critical files are empty"
    exit 1
fi

# Performance check
echo "6. Performance metrics..."
RUNTIME=$(grep "Total time:" /tmp/tutrast_output.log | awk '{print $3}')
echo "   Runtime: $RUNTIME seconds"
if [ ! -z "$RUNTIME" ]; then
    echo "   ✓ Performance data captured"
fi

echo
echo "=========================================="
echo "✓ All verification tests passed!"
echo "=========================================="
echo
echo "Key statistics from run:"
echo "---"
grep -E "(Total clusters|Total TS points|Found .* tunnels|Generated .* processes)" /tmp/tutrast_output.log
echo "---"
