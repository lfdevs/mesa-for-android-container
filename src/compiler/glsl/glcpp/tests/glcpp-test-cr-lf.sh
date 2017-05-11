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

testdir="$srcdir/glsl/glcpp/tests"
glcpp_test="$srcdir/glsl/glcpp/tests/glcpp-test.sh"

total=0
pass=0

# This supports a pipe that doesn't destroy the exit status of first command
#
# http://unix.stackexchange.com/questions/14270/get-exit-status-of-process-thats-piped-to-another
stdintoexitstatus() {
    read exitstatus
    return $exitstatus
}

run_test ()
{
    cmd="$1"

    total=$((total+1))

    if [ "$VERBOSE" = "yes" ]; then
	if $cmd; then
	    echo "PASS"
	    pass=$((pass+1))
	else
	    echo "FAIL"
	fi
    else
	# This is "$cmd | tail -2" but with the exit status of "$cmd" not "tail -2"
	if (((($cmd; echo $? >&3) | tail -2 | head -1 >&4) 3>&1) | stdintoexitstatus) 4>&1; then
	    echo "PASS"
	    pass=$((pass+1))
	else
	    echo "FAIL"
	fi
    fi
}

usage ()
{
	cat <<EOF
Usage: `basename "$0"` [options...]

Run the entire glcpp-test suite several times, each time with each source
file transformed to use a non-standard line-termination character. Each
entire run with a different line-termination character is considered a
single test.

Valid options include:

	-v|--verbose	Print all output from the various sub-tests
EOF
}

# Parse command-line options
for option; do
    case "${option}" in
	-v|--verbose)
	    VERBOSE=yes;
	    ;;
	*)
	    echo "Unrecognized option: $option" >&2
	    echo >&2
	    usage
	    exit 1
	    ;;
	esac
done

# All tests depend on the .out files being present. So first do a
# normal run of the test suite, (silently) just to create the .out
# files as a side effect.
rm -rf ./subtest-lf
mkdir subtest-lf
for file in "$testdir"/*.c; do
    base=$(basename "$file")
    cp "$file" subtest-lf
done

${glcpp_test} --testdir=subtest-lf >/dev/null 2>&1

echo "===== Testing with \\\\r line terminators (old Mac format) ====="

# Prepare test files with '\r' instead of '\n'
rm -rf ./subtest-cr
mkdir subtest-cr
for file in "$testdir"/*.c; do
    base=$(basename "$file")
    tr "\n" "\r" < "$file" > subtest-cr/"$base"
    cp $abs_builddir/glsl/glcpp/tests/subtest-lf/"$base".out subtest-cr/"$base".expected
done

run_test "${glcpp_test} --testdir=subtest-cr"

echo "===== Testing with \\\\r\\\\n line terminators (DOS format) ====="

# Prepare test files with '\r\n' instead of '\n'
rm -rf ./subtest-cr-lf
mkdir subtest-cr-lf
for file in "$testdir"/*.c; do
    base=$(basename "$file")
    sed -e 's/$//' < "$file" > subtest-cr-lf/"$base"
    cp $abs_builddir/glsl/glcpp/tests/subtest-lf/"$base".out subtest-cr-lf/"$base".expected
done

run_test "${glcpp_test} --testdir=subtest-cr-lf"

echo "===== Testing with \\\\n\\\\r (bizarre, but allowed by GLSL spec.) ====="

# Prepare test files with '\n\r' instead of '\n'
rm -rf ./subtest-lf-cr
mkdir subtest-lf-cr
for file in "$testdir"/*.c; do
    base=$(basename "$file")
    sed -e 's/$//' < "$file" | tr "\n\r" "\r\n" > subtest-lf-cr/"$base"
    cp $abs_builddir/glsl/glcpp/tests/subtest-lf/"$base".out subtest-lf-cr/"$base".expected
done

run_test "${glcpp_test} --testdir=subtest-lf-cr"

if [ $total -eq 0 ]; then
    echo "Could not find any tests."
    exit 1
fi

echo ""
echo "$pass/$total tests returned correct results"
echo ""

if [ "$pass" = "$total" ]; then
    exit 0
else
    exit 1
fi
