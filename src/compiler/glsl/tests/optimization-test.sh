#!/bin/sh

if [ -z "$PYTHON2" ]; then
    PYTHON2=python2
fi

which $PYTHON2 >/dev/null
if [ $? -ne 0 ]; then
    echo "Could not find python2. Make sure that PYTHON2 variable is correctly set."
    exit 1
fi

if [ -z "$srcdir" -o -z "$abs_builddir" ]; then
    echo ""
    echo "Warning: you're invoking the script manually and things may fail."
    echo "Attempting to determine/set srcdir and abs_builddir variables."
    echo ""

    # Variable should point to the Makefile.glsl.am
    srcdir=./../../
    cd `dirname "$0"`
    # Variable should point to the folder two levels above glsl_test
    abs_builddir=`pwd`/../../
fi

compare_ir=$srcdir/glsl/tests/compare_ir.py

total=0
pass=0
has_tests=0

# Store our location before we start diving into subdirectories.
ORIGDIR=`pwd`
echo "======       Generating tests      ======"
for dir in $srcdir/glsl/tests/*/; do
    if [ -e "${dir}create_test_cases.py" ]; then
        echo "$dir"
        # construct the correct builddir
        completedir="$abs_builddir/glsl/tests/`echo ${dir} | sed 's|.*/glsl/tests/||g'`"
        mkdir -p $completedir
        cd $dir;
        $PYTHON2 create_test_cases.py --runner $abs_builddir/glsl/glsl_test --outdir $completedir;
        if [ $? -eq 0 ]; then
            has_tests=1
        fi
        cd ..
    fi
done
cd "$ORIGDIR"

if [ $has_tests -eq 0 ]; then
    echo "Could not generate any tests."
    exit 1
fi

if [ ! -f "$compare_ir" ]; then
    echo "Could not find compare_ir. Make sure that srcdir variable is correctly set."
    exit 1
fi

echo "====== Testing optimization passes ======"
for test in `find . -iname '*.opt_test'`; do
    echo -n "Testing `echo $test| sed 's|.*/glsl/tests/||g'`..."
    ./$test > "$test.out" 2>&1
    total=$((total+1))
    if $PYTHON2 $PYTHON_FLAGS $compare_ir "$test.expected" "$test.out" >/dev/null 2>&1; then
        echo "PASS"
        pass=$((pass+1))
    else
        echo "FAIL"
        $PYTHON2 $PYTHON_FLAGS $compare_ir "$test.expected" "$test.out"
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
