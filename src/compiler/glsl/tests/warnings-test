#!/usr/bin/env bash

# Execute several shaders, and check that the InfoLog outcome is the expected.

compiler=./glsl_compiler
total=0
pass=0

echo "====== Testing compilation output ======"
for test in `find . -iname '*.vert'`; do
    echo -n "Testing $test..."
    $compiler --just-log --version 150 "$test" > "$test.out" 2>&1
    total=$((total+1))
    if diff "$test.expected" "$test.out" >/dev/null 2>&1; then
        echo "PASS"
        pass=$((pass+1))
    else
        echo "FAIL"
        diff "$test.expected" "$test.out"
    fi
done

echo ""
echo "$pass/$total tests returned correct results"
echo ""

if [[ $pass == $total ]]; then
    exit 0
else
    exit 1
fi
