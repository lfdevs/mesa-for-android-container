from io import StringIO
from typing import TYPE_CHECKING, Any

import re
from lava.utils.lava_farm import LavaFarm, get_lava_farm
from ruamel.yaml.scalarstring import LiteralScalarString
from ruamel.yaml import YAML
from os import getenv

if TYPE_CHECKING:
    from lava.lava_job_submitter import LAVAJobSubmitter

# How many attempts should be made when a timeout happen during LAVA device boot.
NUMBER_OF_ATTEMPTS_LAVA_BOOT = int(getenv("LAVA_NUMBER_OF_ATTEMPTS_LAVA_BOOT", 3))

# Supports any integers in [0, 100].
# The scheduler considers the job priority when ordering the queue
# to consider which job should run next.
JOB_PRIORITY = int(getenv("JOB_PRIORITY", 75))


def has_ssh_support(job_submitter: "LAVAJobSubmitter") -> bool:
    force_uart = bool(getenv("LAVA_FORCE_UART", False))

    if force_uart:
        return False

    # Only Collabora's farm supports to run docker container as a LAVA actions,
    # which is required to follow the job in a SSH section
    current_farm = get_lava_farm()

    # SSH job definition still needs to add support for fastboot.
    job_uses_fastboot: bool = job_submitter.boot_method == "fastboot"

    return current_farm == LavaFarm.COLLABORA and not job_uses_fastboot


def generate_lava_yaml_payload(job_submitter: "LAVAJobSubmitter") -> dict[str, Any]:
    """
    Bridge function to use the supported job definition depending on some Mesa
    CI job characteristics.

    The strategy here, is to use LAVA with a containerized SSH session to follow
    the job output, escaping from dumping data to the UART, which proves to be
    error prone in some devices.
    """
    from lava.utils.ssh_job_definition import (
        generate_lava_yaml_payload as ssh_lava_yaml,
    )
    from lava.utils.uart_job_definition import (
        generate_lava_yaml_payload as uart_lava_yaml,
    )

    if has_ssh_support(job_submitter):
        return ssh_lava_yaml(job_submitter)

    return uart_lava_yaml(job_submitter)


def generate_lava_job_definition(job_submitter: "LAVAJobSubmitter") -> str:
    job_stream = StringIO()
    yaml = YAML()
    yaml.width = 4096
    yaml.dump(generate_lava_yaml_payload(job_submitter), job_stream)
    return job_stream.getvalue()


def to_yaml_block(steps_array: list[str], escape_vars=[]) -> LiteralScalarString:
    def escape_envvar(match):
        return "\\" + match.group(0)

    filtered_array = [s for s in steps_array if s.strip() and not s.startswith("#")]
    final_str = "\n".join(filtered_array)

    for escape_var in escape_vars:
        # Find env vars and add '\\' before them
        final_str = re.sub(rf"\${escape_var}*", escape_envvar, final_str)
    return LiteralScalarString(final_str)


def generate_metadata(args) -> dict[str, Any]:
    # General metadata and permissions
    values = {
        "job_name": f"mesa: {args.pipeline_info}",
        "device_type": args.device_type,
        "visibility": {"group": [args.visibility_group]},
        "priority": JOB_PRIORITY,
        "context": {
            "extra_nfsroot_args": " init=/init rootwait usbcore.quirks=0bda:8153:k"
        },
        "timeouts": {
            "job": {"minutes": args.job_timeout_min},
            "actions": {
                "depthcharge-retry": {
                    # Could take between 1 and 1.5 min in slower boots
                    "minutes": 4
                },
                "depthcharge-start": {
                    # Should take less than 1 min.
                    "minutes": 1,
                },
                "depthcharge-action": {
                    # This timeout englobes the entire depthcharge timing,
                    # including retries
                    "minutes": 5
                    * NUMBER_OF_ATTEMPTS_LAVA_BOOT,
                },
            },
        },
    }

    if args.lava_tags:
        values["tags"] = args.lava_tags.split(",")

    return values


def artifact_download_steps(args):
    """
    This function is responsible for setting up the SSH server in the DUT and to
    export the first boot environment to a file.
    """
    # Putting JWT pre-processing and mesa download, within init-stage1.sh file,
    # as we do with non-SSH version.
    download_steps = [
        "set -ex",
        "curl -L --retry 4 -f --retry-all-errors --retry-delay 60 "
        f"{args.job_rootfs_overlay_url} | tar -xz -C /",
        f"mkdir -p {args.ci_project_dir}",
        f"curl -L --retry 4 -f --retry-all-errors --retry-delay 60 {args.build_url} | "
        f"tar --zstd -x -C {args.ci_project_dir}",
    ]

    # If the JWT file is provided, we will use it to authenticate with the cloud
    # storage provider and will hide it from the job output in Gitlab.
    if args.jwt_file:
        with open(args.jwt_file) as jwt_file:
            download_steps += [
                "set +x  # HIDE_START",
                f'echo -n "{jwt_file.read()}" > "{args.jwt_file}"',
                "set -x  # HIDE_END",
                f'echo "export CI_JOB_JWT_FILE={args.jwt_file}" >> /set-job-env-vars.sh',
            ]
    else:
        download_steps += [
            "echo Could not find jwt file, disabling S3 requests...",
            "sed -i '/S3_RESULTS_UPLOAD/d' /set-job-env-vars.sh",
        ]

    return download_steps
