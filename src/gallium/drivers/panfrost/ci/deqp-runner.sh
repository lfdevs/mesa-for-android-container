#!/bin/sh

set -x

DEQP_OPTIONS="--deqp-surface-width=256 --deqp-surface-height=256"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-visibility=hidden"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-log-images=disable"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-watchdog=enable"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-crashhandler=enable"

export XDG_RUNTIME_DIR=/tmp
export LIBGL_DRIVERS_PATH=/mesa/lib/dri/
export LD_LIBRARY_PATH=/mesa/lib/
export XDG_CONFIG_HOME=$(pwd)
export MESA_GLES_VERSION_OVERRIDE=3.0

echo "[core]\nidle-time=0\nrequire-input=false\n[shell]\nlocking=false" > weston.ini

cd /deqp/modules/gles2

# Generate test case list file
weston --tty=7 &
sleep 1  # Give some time for Weston to start up
./deqp-gles2 $DEQP_OPTIONS --deqp-runmode=stdout-caselist | grep "TEST: dEQP-GLES2" | cut -d ' ' -f 2 > /tmp/case-list.txt

# Disable for now tests that are very slow, either by just using lots of CPU or by crashing
sed -i '/dEQP-GLES2.performance/d' /tmp/case-list.txt
sed -i '/dEQP-GLES2.stress/d' /tmp/case-list.txt
sed -i '/dEQP-GLES2.functional.fbo.render.depth./d' /tmp/case-list.txt
sed -i '/dEQP-GLES2.functional.flush_finish./d' /tmp/case-list.txt

# Cannot use tee because dash doesn't have pipefail
touch /tmp/result.txt
tail -f /tmp/result.txt &

while [ -s /tmp/case-list.txt ]; do
	./deqp-gles2 $DEQP_OPTIONS --deqp-log-filename=/dev/null --deqp-caselist-file=/tmp/case-list.txt >> /tmp/result.txt
	if [ $? -ne 0 ]; then
		# Continue from the subtest after the failing one
		crashed_test=$(grep "Test case" /tmp/result.txt | tail -1 | sed "s/Test case '\(.*\)'\.\./\1/")
		sed -i "0,/^$crashed_test$/d" /tmp/case-list.txt

		# So LAVA knows what happened
		echo "Test case '$crashed_test'..
                         Crash"
	else
		break
	fi
done
