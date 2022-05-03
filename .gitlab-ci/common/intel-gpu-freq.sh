#!/bin/sh
#
# The Intel i915 GPU driver allows to change the minimum, maximum and boost
# frequencies in steps of 50 MHz via /sys/class/drm/card<n>/<freq_info>,
# where <n> is the DRM card index and <freq_info> one of the following:
#
# - gt_max_freq_mhz (enforced maximum freq)
# - gt_min_freq_mhz (enforced minimum freq)
# - gt_boost_freq_mhz (enforced boost freq)
#
# The hardware capabilities can be accessed via:
#
# - gt_RP0_freq_mhz (supported maximum freq)
# - gt_RPn_freq_mhz (supported minimum freq)
# - gt_RP1_freq_mhz (most efficient freq)
#
# The current frequency can be read from:
# - gt_act_freq_mhz (the actual GPU freq)
# - gt_cur_freq_mhz (the last requested freq)
#
# Copyright (C) 2022 Collabora Ltd.
# Author: Cristian Ciocaltea <cristian.ciocaltea@collabora.com>
#
# SPDX-License-Identifier: MIT
#

#
# Constants
#
DRM_FREQ_SYSFS_PATTERN="/sys/class/drm/card%d/gt_%s_freq_mhz"
ENF_FREQ_INFO="max min boost"
CAP_FREQ_INFO="RP0 RPn RP1"
ACT_FREQ_INFO="act cur"
THROTT_DETECT_SLEEP_SEC=2
THROTT_DETECT_PID_FILE_PATH=/tmp/thrott-detect.pid

#
# Global variables.
#
unset INTEL_DRM_CARD_INDEX
unset GET_ACT_FREQ GET_ENF_FREQ GET_CAP_FREQ
unset SET_MIN_FREQ SET_MAX_FREQ
unset MONITOR_FREQ
unset DETECT_THROTT
unset DRY_RUN

#
# Simple printf based stderr logger.
#
log() {
    local msg_type=$1

    shift
    printf "%s: %s: " "${msg_type}" "${0##*/}" >&2
    printf "$@" >&2
    printf "\n" >&2
}

#
# Helper to print sysfs path for the given card index and freq info.
#
# arg1: Frequency info sysfs name, one of *_FREQ_INFO constants above
# arg2: Video card index, defaults to INTEL_DRM_CARD_INDEX
#
print_freq_sysfs_path() {
    printf ${DRM_FREQ_SYSFS_PATTERN} "${2:-${INTEL_DRM_CARD_INDEX}}" "$1"
}

#
# Helper to set INTEL_DRM_CARD_INDEX for the first identified Intel video card.
#
identify_intel_gpu() {
    local i=0 vendor path

    while [ ${i} -lt 16 ]; do
        [ -c "/dev/dri/card$i" ] || {
            i=$((i + 1))
            continue
        }

        path=$(print_freq_sysfs_path "" ${i})
        path=${path%/*}/device/vendor

        [ -r "${path}" ] && read vendor < "${path}" && \
            [ "${vendor}" = "0x8086" ] && INTEL_DRM_CARD_INDEX=$i && return 0

        i=$((i + 1))
    done

    return 1
}

#
# Read the specified freq info from sysfs.
#
# arg1: Flag (y/n) to also enable printing the freq info.
# arg2...: Frequency info sysfs name(s), see *_FREQ_INFO constants above
# return: Global variable(s) FREQ_${arg} containing the requested information
#
read_freq_info() {
    local var val path print=0 ret=0

    [ "$1" = "y" ] && print=1
    shift

    while [ $# -gt 0 ]; do
        var=FREQ_$1
        path=$(print_freq_sysfs_path "$1")

        [ -r ${path} ] && read ${var} < ${path} || {
            log ERROR "Failed to read freq info from: %s" "${path}"
            ret=1
            continue
        }

        [ -n "${var}" ] || {
            log ERROR "Got empty freq info from: %s" "${path}"
            ret=1
            continue
        }

        [ ${print} -eq 1 ] && {
            eval val=\$${var}
            printf "%6s: %4s MHz\n" "$1" "${val}"
        }

        shift
    done

    return ${ret}
}

#
# Display requested info.
#
print_freq_info() {
    local req_freq

    [ -n "${GET_CAP_FREQ}" ] && {
        printf "* Hardware capabilities\n"
        read_freq_info y ${CAP_FREQ_INFO}
        printf "\n"
    }

    [ -n "${GET_ENF_FREQ}" ] && {
        printf "* Enforcements\n"
        read_freq_info y ${ENF_FREQ_INFO}
        printf "\n"
    }

    [ -n "${GET_ACT_FREQ}" ] && {
        printf "* Actual\n"
        read_freq_info y ${ACT_FREQ_INFO}
        printf "\n"
    }
}

#
# Helper to print frequency value as requested by user via '-s, --set' option.
# arg1: user requested freq value
#
compute_freq_set() {
    local val

    case "$1" in
    +)
        val=${FREQ_RP0}
        ;;
    -)
        val=${FREQ_RPn}
        ;;
    *%)
        val=$((${1%?} * ${FREQ_RP0} / 100))
        # Adjust freq to comply with 50 MHz increments
        val=$((val / 50 * 50))
        ;;
    *[!0-9]*)
        log ERROR "Cannot set freq to invalid value: %s" "$1"
        return 1
        ;;
    "")
        log ERROR "Cannot set freq to unspecified value"
        return 1
        ;;
    *)
        # Adjust freq to comply with 50 MHz increments
        val=$(($1 / 50 * 50))
        ;;
    esac

    printf "%s" "${val}"
}

#
# Helper for set_freq().
#
set_freq_max() {
    log INFO "Setting GPU max freq to %s MHz" "${SET_MAX_FREQ}"

    read_freq_info n min || return $?

    [ ${SET_MAX_FREQ} -gt ${FREQ_RP0} ] && {
        log ERROR "Cannot set GPU max freq (%s) to be greater than hw max freq (%s)" \
            "${SET_MAX_FREQ}" "${FREQ_RP0}"
        return 1
    }

    [ ${SET_MAX_FREQ} -lt ${FREQ_RPn} ] && {
        log ERROR "Cannot set GPU max freq (%s) to be less than hw min freq (%s)" \
            "${SET_MIN_FREQ}" "${FREQ_RPn}"
        return 1
    }

    [ ${SET_MAX_FREQ} -lt ${FREQ_min} ] && {
        log ERROR "Cannot set GPU max freq (%s) to be less than min freq (%s)" \
            "${SET_MAX_FREQ}" "${FREQ_min}"
        return 1
    }

    [ -z "${DRY_RUN}" ] || return 0

    printf "%s" ${SET_MAX_FREQ} | tee $(print_freq_sysfs_path max) \
        $(print_freq_sysfs_path boost) > /dev/null
    [ $? -eq 0 ] || {
        log ERROR "Failed to set GPU max frequency"
        return 1
    }
}

#
# Helper for set_freq().
#
set_freq_min() {
    log INFO "Setting GPU min freq to %s MHz" "${SET_MIN_FREQ}"

    read_freq_info n max || return $?

    [ ${SET_MIN_FREQ} -gt ${FREQ_max} ] && {
        log ERROR "Cannot set GPU min freq (%s) to be greater than max freq (%s)" \
            "${SET_MIN_FREQ}" "${FREQ_max}"
        return 1
    }

    [ ${SET_MIN_FREQ} -lt ${FREQ_RPn} ] && {
        log ERROR "Cannot set GPU min freq (%s) to be less than hw min freq (%s)" \
            "${SET_MIN_FREQ}" "${FREQ_RPn}"
        return 1
    }

    [ -z "${DRY_RUN}" ] || return 0

    printf "%s" ${SET_MIN_FREQ} > $(print_freq_sysfs_path min)
    [ $? -eq 0 ] || {
        log ERROR "Failed to set GPU min frequency"
        return 1
    }
}

#
# Set min or max or both GPU frequencies to the user indicated values.
#
set_freq() {
    # Get hw max & min frequencies
    read_freq_info n RP0 RPn || return $?

    [ -z "${SET_MAX_FREQ}" ] || {
        SET_MAX_FREQ=$(compute_freq_set "${SET_MAX_FREQ}")
        [ -z "${SET_MAX_FREQ}" ] && return 1
    }

    [ -z "${SET_MIN_FREQ}" ] || {
        SET_MIN_FREQ=$(compute_freq_set "${SET_MIN_FREQ}")
        [ -z "${SET_MIN_FREQ}" ] && return 1
    }

    #
    # Ensure correct operation order, to avoid setting min freq
    # to a value which is larger than max freq.
    #
    # E.g.:
    #   crt_min=crt_max=600; new_min=new_max=700
    #   > operation order: max=700; min=700
    #
    #   crt_min=crt_max=600; new_min=new_max=500
    #   > operation order: min=500; max=500
    #
    if [ -n "${SET_MAX_FREQ}" ] && [ -n "${SET_MIN_FREQ}" ]; then
        [ ${SET_MAX_FREQ} -lt ${SET_MIN_FREQ} ] && {
            log ERROR "Cannot set GPU max freq to be less than min freq"
            return 1
        }

        read_freq_info n min || return $?

        if [ ${SET_MAX_FREQ} -lt ${FREQ_min} ]; then
            set_freq_min || return $?
            set_freq_max
        else
            set_freq_max || return $?
            set_freq_min
        fi
    elif [ -n "${SET_MAX_FREQ}" ]; then
        set_freq_max
    elif [ -n "${SET_MIN_FREQ}" ]; then
        set_freq_min
    else
        log "Unexpected call to set_freq()"
        return 1
    fi
}

#
# Helper for detect_throttling().
#
get_thrott_detect_pid() {
    [ -e ${THROTT_DETECT_PID_FILE_PATH} ] || return 0

    local pid
    read pid < ${THROTT_DETECT_PID_FILE_PATH} || {
        log ERROR "Failed to read pid from: %s" "${THROTT_DETECT_PID_FILE_PATH}"
        return 1
    }

    local proc_path=/proc/${pid:-invalid}/cmdline
    [ -r ${proc_path} ] && grep -qs "${0##*/}" ${proc_path} && {
        printf "%s" "${pid}"
        return 0
    }

    # Remove orphaned PID file
    rm -rf ${THROTT_DETECT_PID_FILE_PATH}
    return 1
}

#
# Control detection and reporting of GPU throttling events.
# arg1: start - run throttle detector in background
#       stop - stop throttle detector process, if any
#       status - verify if throttle detector is running
#
detect_throttling() {
    local pid
    pid=$(get_thrott_detect_pid)

    case "$1" in
    status)
        printf "Throttling detector is "
        [ -z "${pid}" ] && printf "not running\n" && return 0
        printf "running (pid=%s)\n" ${pid}
        ;;

    stop)
        [ -z "${pid}" ] && return 0

        log INFO "Stopping throttling detector (pid=%s)" "${pid}"
        kill ${pid}; sleep 1; kill -0 ${pid} 2>/dev/null && kill -9 ${pid}
        rm -rf ${THROTT_DETECT_PID_FILE_PATH}
        ;;

    start)
        [ -n "${pid}" ] && {
            log WARN "Throttling detector is already running (pid=%s)" ${pid}
            return 0
        }

        (
            read_freq_info n RPn || exit $?

            while true; do
                sleep ${THROTT_DETECT_SLEEP_SEC}
                read_freq_info n act min cur || exit $?

                #
                # The throttling seems to occur when act freq goes below min.
                # However, it's necessary to exclude the idle states, where
                # act freq normally reaches RPn and cur goes below min.
                #
                [ ${FREQ_act} -lt ${FREQ_min} ] && \
                [ ${FREQ_act} -gt ${FREQ_RPn} ] && \
                [ ${FREQ_cur} -ge ${FREQ_min} ] && \
                    printf "GPU throttling detected: act=%s min=%s cur=%s RPn=%s\n" \
                    ${FREQ_act} ${FREQ_min} ${FREQ_cur} ${FREQ_RPn}
            done
        ) &

        pid=$!
        log INFO "Started GPU throttling detector (pid=%s)" ${pid}

        printf "%s\n" ${pid} > ${THROTT_DETECT_PID_FILE_PATH} || \
            log WARN "Failed to write throttle detector PID file"
        ;;
    esac
}

#
# Show help message.
#
print_usage() {
    cat <<EOF
Usage: ${0##*/} [OPTION]...

A script to manage Intel GPU frequencies. Can be used for debugging performance
problems or trying to obtain a stable frequency while benchmarking.

Note Intel GPUs only accept specific frequencies, usually multiples of 50 MHz.

Options:
  -g, --get [act|enf|cap|all]
                        Get frequency information: active (default), enforced,
                        hardware capabilities or all of them.

  -s, --set [{min|max}=]{FREQUENCY[%]|+|-}
                        Set min or max frequency to the given value (MHz).
                        Append '%' to interpret FREQUENCY as % of hw max.
                        Use '+' or '-' to set frequency to hardware max or min.
                        Omit min/max prefix to set both frequencies.

  -r, --reset           Reset frequencies to hardware defaults.

  -m, --monitor [act|enf|cap|all]
                        Monitor the indicated frequencies via 'watch' utility.
                        See '-g, --get' option for more details.

  -d|--detect-thrott [start|stop|status]
                        Start (default operation) the throttling detector
                        as a background process. Use 'stop' or 'status' to
                        terminate the detector process or verify its status.

  --dry-run             See what the script will do without applying any
                        frequency changes.

  -h, --help            Display this help text and exit.
EOF
}

#
# Parse user input for '-g, --get' option.
# Returns 0 if a value has been provided, otherwise 1.
#
parse_option_get() {
    local ret=0

    case "$1" in
    act) GET_ACT_FREQ=1;;
    enf) GET_ENF_FREQ=1;;
    cap) GET_CAP_FREQ=1;;
    all) GET_ACT_FREQ=1; GET_ENF_FREQ=1; GET_CAP_FREQ=1;;
    -*|"")
        # No value provided, using default.
        GET_ACT_FREQ=1
        ret=1
        ;;
    *)
        print_usage
        exit 1
        ;;
    esac

    return ${ret}
}

#
# Validate user input for '-s, --set' option.
#
validate_option_set() {
    case "$1" in
    +|-|[0-9]%|[0-9][0-9]%)
        return 0
        ;;
    *[!0-9]*|"")
        print_usage
        exit 1
        ;;
    esac
}

#
# Parse script arguments.
#
[ $# -eq 0 ] && { print_usage; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
    -g|--get)
        parse_option_get "$2" && shift
        ;;

    -s|--set)
        shift
        case "$1" in
        min=*)
            SET_MIN_FREQ=${1#min=}
            validate_option_set "${SET_MIN_FREQ}"
            ;;
        max=*)
            SET_MAX_FREQ=${1#max=}
            validate_option_set "${SET_MAX_FREQ}"
            ;;
        *)
            SET_MIN_FREQ=$1
            validate_option_set "${SET_MIN_FREQ}"
            SET_MAX_FREQ=${SET_MIN_FREQ}
            ;;
        esac
        ;;

    -r|--reset)
        RESET_FREQ=1
        SET_MIN_FREQ="-"
        SET_MAX_FREQ="+"
        ;;

    -m|--monitor)
        MONITOR_FREQ=act
        parse_option_get "$2" && MONITOR_FREQ=$2 && shift
        ;;

    -d|--detect-thrott)
        DETECT_THROTT=start
        case "$2" in
        start|stop|status)
            DETECT_THROTT=$2
            shift
            ;;
        esac
        ;;

    --dry-run)
        DRY_RUN=1
        ;;

    -h|--help)
        print_usage
        exit 0
        ;;

    *)
        print_usage
        exit 1
        ;;
    esac

    shift
done

#
# Main
#
RET=0

identify_intel_gpu || {
    log INFO "No Intel GPU detected"
    exit 0
}

[ -n "${SET_MIN_FREQ}${SET_MAX_FREQ}" ] && { set_freq || RET=$?; }
print_freq_info

[ -n "${DETECT_THROTT}" ] && detect_throttling ${DETECT_THROTT}

[ -n "${MONITOR_FREQ}" ] && {
    log INFO "Entering frequency monitoring mode"
    sleep 2
    exec watch -d -n 1 "$0" -g "${MONITOR_FREQ}"
}

exit ${RET}
