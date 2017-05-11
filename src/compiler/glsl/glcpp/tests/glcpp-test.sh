#!/bin/sh

if [ -z "$srcdir" -o -z "$abs_builddir" ]; then
    echo ""
    echo "Warning: you're invoking the script manually and things may fail."
    echo "Attempting to determine/set srcdir and abs_builddir variables."
    echo ""

    # Should point to `dirname Makefile.glsl.am`
    srcdir=./../../../
    cd `dirname "$0"`
    # Should point to `dirname Makefile` equivalent to the above.
    abs_builddir=`pwd`/../../../
fi

testdir=$srcdir/glsl/glcpp/tests
outdir=$abs_builddir/glsl/glcpp/tests
glcpp=$abs_builddir/glsl/glcpp/glcpp

trap 'rm $test.valgrind-errors; exit 1' INT QUIT

usage ()
{
    cat <<EOF
Usage: `basename "$0"` [options...]

Run the test suite for mesa's GLSL pre-processor.

Valid options include:

	--testdir=<DIR>	Use tests in the given <DIR> (default is ".")
	--valgrind	Run the test suite a second time under valgrind
EOF
}

test_specific_args ()
{
    test="$1"

    tr "\r" "\n" < "$test" | grep 'glcpp-args:' | sed -e 's,^.*glcpp-args: *,,'
}

# Parse command-line options
for option; do
    case "${option}" in
        "--help")
            usage
            exit 0
            ;;
        "--valgrind")
	    do_valgrind=yes
            ;;
        "--testdir="*)
            testdir="${option#--testdir=}"
            outdir="${outdir}/${option#--testdir=}"
            ;;
        *)
	    echo "Unrecognized option: $option" >&2
	    echo >&2
	    usage
	    exit 1
            ;;
        esac
done

total=0
pass=0
clean=0

mkdir -p $outdir

echo "====== Testing for correctness ======"
for test in $testdir/*.c; do
    out=$outdir/${test##*/}.out

    printf "Testing `basename $test`... "
    $glcpp $(test_specific_args $test) < $test > $out 2>&1
    total=$((total+1))
    if cmp $test.expected $out >/dev/null 2>&1; then
	echo "PASS"
	pass=$((pass+1))
    else
	echo "FAIL"
	diff -u $test.expected $out
    fi
done

if [ $total -eq 0 ]; then
    echo "Could not find any tests."
    exit 1
fi

echo ""
echo "$pass/$total tests returned correct results"
echo ""

if [ "$do_valgrind" = "yes" ]; then
    echo "====== Testing for valgrind cleanliness ======"
    for test in $testdir/*.c; do
	printf "Testing `basename $test` with valgrind..."
	valgrind --error-exitcode=31 --log-file=$test.valgrind-errors $glcpp $(test_specific_args $test) < $test >/dev/null 2>&1
	if [ "$?" = "31" ]; then
	    echo "ERRORS"
	    cat $test.valgrind-errors
	else
	    echo "CLEAN"
	    clean=$((clean+1))
	    rm $test.valgrind-errors
	fi
    done

    echo ""
    echo "$pass/$total tests returned correct results"
    echo "$clean/$total tests are valgrind-clean"
fi

if [ "$pass" = "$total" ] && [ "$do_valgrind" != "yes" ] || [ "$pass" = "$total" ]; then
    exit 0
else
    exit 1
fi

