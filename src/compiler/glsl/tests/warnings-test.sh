#!/bin/sh

if [ -z "$srcdir" -o -z "$abs_builddir" ]; then
    echo ""
    echo "Warning: you're invoking the script manually and things may fail."
    echo "Attempting to determine/set srcdir and abs_builddir variables."
    echo ""

    # Variable should point to the Makefile.glsl.am
    srcdir=./../../
    cd `dirname "$0"`
    # Variable should point to glsl_compiler
    abs_builddir=`pwd`/../../
fi

# Execute several shaders, and check that the InfoLog outcome is the expected.

compiler=$abs_builddir/glsl_compiler
total=0
pass=0

if [ ! -x "$compiler" ]; then
    echo "Could not find glsl_compiler. Ensure that it is build via make check"
    exit 1
fi

tests_relative_dir="glsl/tests/warnings"

echo "====== Testing compilation output ======"
for test in $srcdir/$tests_relative_dir/*.vert; do
    test_output="$abs_builddir/$tests_relative_dir/`basename $test`"
    mkdir -p $abs_builddir/$tests_relative_dir/
    echo -n "Testing `basename $test`..."
    $compiler --just-log --version 150 "$test" > "$test_output.out" 2>&1
    total=$((total+1))
    if diff "$test.expected" "$test_output.out" >/dev/null 2>&1; then
        echo "PASS"
        pass=$((pass+1))
    else
        echo "FAIL"
        diff "$test.expected" "$test_output.out"
    fi
done

if [ $total -eq 0 ]; then
    echo "Could not find any tests."
    exit 1
fi

echo ""
echo "$pass/$total tests returned correct results"
echo ""

if [ $pass = $total ]; then
    exit 0
else
    exit 1
fi
