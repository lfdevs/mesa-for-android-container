#!/bin/sh

set -e

#
# Helper to generate CIDs for virtio-vsock based communication with processes
# running inside crosvm guests.
#
# A CID is a 32-bit Context Identifier to be assigned to a crosvm instance
# and must be unique across the host system. For this purpose, let's take
# the least significant 25 bits from CI_JOB_ID as a base and generate a 7-bit
# prefix number to handle up to 128 concurrent crosvm instances per job runner.
#
# As a result, the following variables are set:
#  - VSOCK_CID: the crosvm unique CID to be passed as a run argument
#
#  - VSOCK_STDOUT, VSOCK_STDERR: the port numbers the guest should accept
#    vsock connections on in order to transfer output messages
#
#  - VSOCK_TEMP_DIR: the temporary directory path used to pass additional
#    context data towards the guest
#
set_vsock_context() {
    [ -n "${CI_JOB_ID}" ] || {
        echo "Missing or unset CI_JOB_ID env variable" >&2
        exit 1
    }

    local dir_prefix="/tmp-vsock."
    local cid_prefix=0
    unset VSOCK_TEMP_DIR

    while [ ${cid_prefix} -lt 128 ]; do
        VSOCK_TEMP_DIR=${dir_prefix}${cid_prefix}
        mkdir "${VSOCK_TEMP_DIR}" >/dev/null 2>&1 && break || unset VSOCK_TEMP_DIR
        cid_prefix=$((cid_prefix + 1))
    done

    [ -n "${VSOCK_TEMP_DIR}" ] || return 1

    VSOCK_CID=$(((CI_JOB_ID & 0x1ffffff) | ((cid_prefix & 0x7f) << 25)))
    VSOCK_STDOUT=5001
    VSOCK_STDERR=5002

    return 0
}

# The dEQP binary needs to run from the directory it's in
if [ -n "${1##*.sh}" ] && [ -z "${1##*"deqp"*}" ]; then
    DEQP_BIN_DIR=$(dirname "$1")
    export DEQP_BIN_DIR
fi

set_vsock_context || { echo "Could not generate crosvm vsock CID" >&2; exit 1; }

# Ensure cleanup on script exit
trap 'exit ${exit_code}' INT TERM
trap 'exit_code=$?; [ -z "${CROSVM_PID}${SOCAT_PIDS}" ] || kill ${CROSVM_PID} ${SOCAT_PIDS} >/dev/null 2>&1 || true; rm -rf ${VSOCK_TEMP_DIR}' EXIT

# Securely pass the current variables to the crosvm environment
echo "Variables passed through:"
SCRIPT_DIR=$(readlink -en "${0%/*}")
${SCRIPT_DIR}/common/generate-env.sh | tee ${VSOCK_TEMP_DIR}/crosvm-env.sh

# Set the crosvm-script as the arguments of the current script
echo "$@" > ${VSOCK_TEMP_DIR}/crosvm-script.sh

# Setup networking
/usr/sbin/iptables-legacy -w -t nat -A POSTROUTING -o eth0 -j MASQUERADE
echo 1 > /proc/sys/net/ipv4/ip_forward

# Start background processes to receive output from guest
socat -u vsock-connect:${VSOCK_CID}:${VSOCK_STDERR},retry=200,interval=0.1 stderr &
SOCAT_PIDS=$!
socat -u vsock-connect:${VSOCK_CID}:${VSOCK_STDOUT},retry=200,interval=0.1 stdout &
SOCAT_PIDS="${SOCAT_PIDS} $!"

# Prepare to start crosvm
unset DISPLAY
unset XDG_RUNTIME_DIR

CROSVM_KERN_ARGS="quiet console=null root=my_root rw rootfstype=virtiofs ip=192.168.30.2::192.168.30.1:255.255.255.0:crosvm:eth0"
CROSVM_KERN_ARGS="${CROSVM_KERN_ARGS} init=${SCRIPT_DIR}/crosvm-init.sh -- ${VSOCK_STDOUT} ${VSOCK_STDERR} ${VSOCK_TEMP_DIR}"

[ "${CROSVM_GALLIUM_DRIVER}" = "llvmpipe" ] && \
    CROSVM_LIBGL_ALWAYS_SOFTWARE=true || CROSVM_LIBGL_ALWAYS_SOFTWARE=false

set +e -x

# We aren't testing the host driver here, so we don't need to validate NIR on the host
NIR_DEBUG="novalidate" \
LIBGL_ALWAYS_SOFTWARE=${CROSVM_LIBGL_ALWAYS_SOFTWARE} \
GALLIUM_DRIVER=${CROSVM_GALLIUM_DRIVER} \
crosvm run \
    --gpu "${CROSVM_GPU_ARGS}" -m 4096 -c 2 --disable-sandbox \
    --shared-dir /:my_root:type=fs:writeback=true:timeout=60:cache=always \
    --host_ip "192.168.30.1" --netmask "255.255.255.0" --mac "AA:BB:CC:00:00:12" \
    --cid ${VSOCK_CID} -p "${CROSVM_KERN_ARGS}" \
    /lava-files/${KERNEL_IMAGE_NAME:-bzImage} > ${VSOCK_TEMP_DIR}/crosvm 2>&1 &

# Wait for crosvm process to terminate
CROSVM_PID=$!
wait ${CROSVM_PID}
CROSVM_RET=$?
unset CROSVM_PID

[ ${CROSVM_RET} -eq 0 ] && {
    # socat background processes terminate gracefully on remote peers exit
    wait
    unset SOCAT_PIDS
    # The actual return code is the crosvm guest script's exit code
    CROSVM_RET=$(cat ${VSOCK_TEMP_DIR}/exit_code 2>/dev/null)
    # Force error when the guest script's exit code is not available
    CROSVM_RET=${CROSVM_RET:-1}
}

# Show crosvm output on error to help with debugging
[ ${CROSVM_RET} -eq 0 ] || {
    set +x
    echo "Dumping crosvm output.." >&2
    cat ${VSOCK_TEMP_DIR}/crosvm >&2
    set -x
}

exit ${CROSVM_RET}
