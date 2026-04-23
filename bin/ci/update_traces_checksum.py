#!/usr/bin/env python3
# Copyright © 2022 Collabora Ltd.
# Authors:
#   David Heidelberg <david.heidelberg@collabora.com>
#
# For the dependencies, see the requirements.txt
# SPDX-License-Identifier: MIT

"""
Helper script to update traces checksums
"""

import argparse
import bz2
import glob
import re
import json
import sys
from ruamel.yaml import YAML
from subprocess import check_output
from tomledit import Document

import gitlab
from gitlab_common import (get_gitlab_project, read_token, wait_for_pipeline,
                           get_gitlab_pipeline_from_url, TOKEN_DIR, get_token_from_default_dir)
from rich import print


DESCRIPTION_FILE = "export PIGLIT_REPLAY_DESCRIPTION_FILE=.*/install/(.*)$"
DEVICE_NAME = "(?:declare -x|export) PIGLIT_REPLAY_DEVICE_NAME='?([^']*)'?$"

def gather_job_results_yml(cur_job):
    log: list[str] = cur_job.trace().decode().splitlines()
    filename: str = ''
    dev_name: str = ''
    for logline in log:
        desc_file = re.search(DESCRIPTION_FILE, logline)
        device_name = re.search(DEVICE_NAME, logline)
        if desc_file:
            filename = desc_file.group(1)
        if device_name:
            dev_name = device_name.group(1)

    if not filename or not dev_name:
        print("[red]Couldn't find device name or YML file in the logs!")
        return

    print(f"👁 Found {dev_name} and file {filename}")

    # find filename in Mesa source
    traces_file = glob.glob('./**/' + filename, recursive=True)
    # write into it
    with open(traces_file[0], 'r', encoding='utf-8') as target_file:
        yaml = YAML()
        yaml.compact(seq_seq=False, seq_map=False)
        yaml.version = 1,2
        yaml.width = 2048  # do not break the text fields
        yaml.default_flow_style = None
        target = yaml.load(target_file)

        # parse artifact
        results_json_bz2 = cur_job.artifact("results/results.json.bz2")
        results_json = bz2.decompress(results_json_bz2).decode("utf-8", errors="replace")
        results = json.loads(results_json)

        for _, value in results["tests"].items():
            if (
                not value['images'] or
                not value['images'][0] or
                "image_desc" not in value['images'][0]
            ):
                continue

            trace: str = value['images'][0]['image_desc']
            checksum: str = value['images'][0]['checksum_render']

            if not checksum:
                print(f"[red]{dev_name}: {trace}: checksum is missing! Crash?")
                continue

            if checksum == "error":
                print(f"[red]{dev_name}: {trace}: crashed")
                continue

            if target['traces'][trace][dev_name].get('checksum') == checksum:
                continue

            if "label" in target['traces'][trace][dev_name]:
                print(
                    f"{dev_name}: {trace}: please verify that label "
                    f"[blue]{target['traces'][trace][dev_name]['label']}[/blue] "
                    "is still valid"
                )

            print(f"[green]{dev_name}: {trace}: checksum updated")
            target['traces'][trace][dev_name]['checksum'] = checksum

    with open(traces_file[0], 'w', encoding='utf-8') as target_file:
        yaml.dump(target, target_file)


def update_toml_checksum(toml_file, trace_path, device, old_checksum, new_checksum):
    with open(toml_file, 'r', encoding='utf-8') as f:
        doc = Document.parse(f.read())

    for table in doc["traces"]:
        if table["path"].value != trace_path:
            continue
        try:
            table["devices"][device]["checksum"] = new_checksum
        except KeyError:
            return False
        with open(toml_file, 'w', encoding='utf-8') as f:
            f.write(doc.as_toml())
        return True

    return False


def gather_job_results_toml(cur_job) -> None:
    """Process checksum-changes.json artifact (gpu-trace-perf replay format)"""
    toml_files = glob.glob('./**/traces-*.toml', recursive=True)

    changes_json = cur_job.artifact("results/checksum-changes.json")
    changes = json.loads(changes_json.decode("utf-8"))

    for change in changes:
        device = change['device']
        trace = change['trace']
        old_checksum = change['old_checksum']
        new_checksum = change['new_checksum']

        if not new_checksum:
            print(f"[red]{device}: {trace}: no new checksum (crash?)")
            continue

        updated = False
        for toml_file in toml_files:
            if update_toml_checksum(toml_file, trace, device, old_checksum, new_checksum):
                print(f"[green]{device}: {trace}: checksum updated")
                updated = True
                break

        if not updated:
            print(f"[red]{device}: {trace}: not found in any traces TOML file")


def gather_results(
    project,
    pipeline,
) -> None:
    """Gather results"""

    target_jobs_regex = re.compile(".*-traces(-restricted)?([:].*)?$")

    for job in pipeline.jobs.list(all=True, sort="desc"):
        if target_jobs_regex.match(job.name) and job.status == "failed":
            cur_job = project.jobs.get(job.id)
            # get variables
            print(f"👁  {job.name}...")
            try:
                gather_job_results_toml(cur_job)
            except Exception:
                gather_job_results_yml(cur_job)


def parse_args() -> None:
    """Parse args"""
    parser = argparse.ArgumentParser(
        description="Tool to generate patch from checksums ",
        epilog="Example: update_traces_checksum.py --rev $(git rev-parse HEAD) "
    )
    parser.add_argument(
        "--rev", metavar="revision", help="repository git revision", default='HEAD'
    )
    parser.add_argument(
        "--token",
        metavar="token",
        type=str,
        default=get_token_from_default_dir(),
        help="Use the provided GitLab token or token file, "
             f"otherwise it's read from {TOKEN_DIR / 'gitlab-token'}",
    )
    parser.add_argument(
        "--pipeline-url",
        metavar="pipeline_url",
        help="specify a pipeline url",
    )
    return parser.parse_args()


if __name__ == "__main__":
    try:
        args = parse_args()

        token = read_token(args.token)

        gl = gitlab.Gitlab(url="https://gitlab.freedesktop.org", private_token=token)

        cur_project = get_gitlab_project(gl, "mesa")

        if args.pipeline_url:
            pipe, cur_project = get_gitlab_pipeline_from_url(gl, args.pipeline_url)
            REV = pipe.sha
        else:
            if not args.rev:
                print('error: the following arguments are required: --rev')
                sys.exit(1)
            sha = check_output(['git', 'rev-parse', args.rev]).decode('ascii').strip()
            print(f"Revision: {sha}")
            (pipe, cur_project) = wait_for_pipeline([cur_project], sha)
        print(f"Pipeline: {pipe.web_url}")
        gather_results(cur_project, pipe)

        sys.exit()
    except KeyboardInterrupt:
        sys.exit(1)
