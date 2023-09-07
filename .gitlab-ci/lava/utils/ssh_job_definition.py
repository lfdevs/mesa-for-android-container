"""
In a few words: some devices in Mesa CI has problematic serial connection, they
may hang (become silent) intermittently. Every time it hangs for minutes, the
job is retried, causing delays in the overall pipeline executing, ultimately
blocking legit MRs to merge.

To reduce reliance on UART, we explored LAVA features, such as running docker
containers as a test alongside the DUT one, to be able to create an SSH server
in the DUT the earliest possible and an SSH client in a docker container, to
establish a SSH session between both, allowing the console output to be passed
via SSH pseudo terminal, instead of relying in the error-prone UART.

In more detail, we aim to use "export -p" to share the initial boot environment
with SSH LAVA test-cases.
The "init-stage1.sh" script handles tasks such as system mounting and network
setup, which are necessary for allocating a pseudo-terminal under "/dev/pts".
Although these chores are not required for establishing an SSH session, they are
essential for proper functionality to the target script given by HWCI_SCRIPT
environment variable.

Therefore, we have divided the job definition into four parts:

1. [DUT] Logging in to DUT and run the SSH server with root access.
2. [DUT] Running the "init-stage1.sh" script for the first SSH test case.
3. [DUT] Export the first boot environment to `/dut-env-vars.sh` file.
4. [SSH] Enabling the pseudo-terminal for colors and running the "init-stage2.sh"
script after sourcing "dut-env-vars.sh" again for the second SSH test case.
"""


from pathlib import Path
from typing import Any

from .lava_job_definition import (
    NUMBER_OF_ATTEMPTS_LAVA_BOOT,
    artifact_download_steps,
    generate_metadata,
    to_yaml_block,
)

# Very early SSH server setup. Uses /dut_ready file to flag it is done.
SSH_SERVER_COMMANDS = {
    "auto_login": {
        "login_commands": [
            "dropbear -R -B",
            "touch /dut_ready",
        ],
        "login_prompt": "ogin:",
        # To login as root, the username should be empty
        "username": "",
    }
}

# TODO: Extract this inline script to a shell file, like we do with
# init-stage[12].sh
# The current way is difficult to maintain because one has to deal with escaping
# characters for both Python and the resulting job definition YAML.
# Plus, it always good to lint bash scripts with shellcheck.
DOCKER_COMMANDS = [
    """set -ex
timeout 1m bash << EOF
while [ -z "$(lava-target-ip)" ]; do
    echo Waiting for DUT to join LAN;
    sleep 1;
done
EOF

ping -c 5 -w 60 $(lava-target-ip)

lava_ssh_test_case() {
    set -x
    local test_case="${1}"
    shift
    lava-test-case \"${test_case}\" --shell \\
        ssh ${SSH_PTY_ARGS:--T} \\
        -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \\
        root@$(lava-target-ip) \"${@}\"
}""",
]


def generate_dut_test(args):
    # Commands executed on DUT.
    # Trying to execute the minimal number of commands, because the console data is
    # retrieved via UART, which is hang-prone in some devices.

    first_stage_steps: list[str] = Path(args.first_stage_init).read_text().splitlines()
    return {
        "namespace": "dut",
        "definitions": [
            {
                "from": "inline",
                "name": "setup-ssh-server",
                "path": "inline-setup-ssh-server",
                "repository": {
                    "metadata": {
                        "format": "Lava-Test Test Definition 1.0",
                        "name": "dut-env-export",
                    },
                    "run": {
                        "steps": [
                            to_yaml_block(first_stage_steps),
                            "export -p > /dut-env-vars.sh",  # Exporting the first boot environment
                        ],
                    },
                },
            }
        ],
    }


def generate_docker_test(args):
    # This is a growing list of commands that will be executed by the docker
    # guest, which will be the SSH client.
    docker_commands = []

    # LAVA test wrapping Mesa CI job in a SSH session.
    init_stages_test = {
        "namespace": "container",
        "timeout": {"minutes": args.job_timeout_min},
        "failure_retry": 3,
        "definitions": [
            {
                "name": "docker_ssh_client",
                "from": "inline",
                "path": "inline/docker_ssh_client.yaml",
                "repository": {
                    "metadata": {
                        "name": "mesa",
                        "description": "Mesa test plan",
                        "format": "Lava-Test Test Definition 1.0",
                    },
                    "run": {"steps": docker_commands},
                },
            }
        ],
        "docker": {
            "image": args.ssh_client_image,
        },
    }

    docker_commands += [
        to_yaml_block(DOCKER_COMMANDS, escape_vars=["LAVA_TARGET_IP"]),
        "lava_ssh_test_case 'wait_for_dut_login' << EOF",
        "while [ ! -e /dut_ready ]; do sleep 1; done;",
        "EOF",
        to_yaml_block(
            (
                "lava_ssh_test_case 'artifact_download' 'bash --' << EOF",
                "source /dut-env-vars.sh",
                *artifact_download_steps(args),
                "EOF",
            )
        ),
        "export SSH_PTY_ARGS=-tt",
        # Putting CI_JOB name as the testcase name, it may help LAVA farm
        # maintainers with monitoring
        f"lava_ssh_test_case 'mesa-ci_{args.mesa_job_name}' "
        # Changing directory to /, as the HWCI_SCRIPT expects that
        "'\"cd / && /init-stage2.sh\"'",
    ]

    return init_stages_test


def generate_lava_yaml_payload(args) -> dict[str, Any]:
    values = generate_metadata(args)

    # URLs to our kernel rootfs to boot from, both generated by the base
    # container build
    deploy = {
        "namespace": "dut",
        "failure_retry": NUMBER_OF_ATTEMPTS_LAVA_BOOT,
        "timeout": {"minutes": 10},
        "timeouts": {"http-download": {"minutes": 2}},
        "to": "tftp",
        "os": "oe",
        "kernel": {"url": f"{args.kernel_url_prefix}/{args.kernel_image_name}"},
        "nfsrootfs": {
            "url": f"{args.rootfs_url_prefix}/lava-rootfs.tar.zst",
            "compression": "zstd",
        },
    }
    if args.kernel_image_type:
        deploy["kernel"]["type"] = args.kernel_image_type
    if args.dtb_filename:
        deploy["dtb"] = {"url": f"{args.kernel_url_prefix}/{args.dtb_filename}.dtb"}

    # always boot over NFS
    boot = {
        "namespace": "dut",
        "failure_retry": NUMBER_OF_ATTEMPTS_LAVA_BOOT,
        "method": args.boot_method,
        "commands": "nfs",
        "prompts": ["lava-shell:"],
        **SSH_SERVER_COMMANDS,
    }

    # only declaring each job as a single 'test' since LAVA's test parsing is
    # not useful to us
    values["actions"] = [
        {"deploy": deploy},
        {"boot": boot},
        {"test": generate_dut_test(args)},
        {"test": generate_docker_test(args)},
    ]

    return values
