#!/usr/bin/env python3
# Copyright © 2020 - 2022 Collabora Ltd.
# Authors:
#   Tomeu Vizoso <tomeu.vizoso@collabora.com>
#   David Heidelberg <david.heidelberg@collabora.com>
#
# TODO GraphQL for dependencies
# SPDX-License-Identifier: MIT

"""
Helper script to restrict running only required CI jobs
and show the job(s) logs.
"""

from typing import Optional
from functools import partial
from concurrent.futures import ThreadPoolExecutor

import os
import re
import time
import argparse
import sys
import gitlab

from colorama import Fore, Style

REFRESH_WAIT_LOG = 10
REFRESH_WAIT_JOBS = 6

URL_START = "\033]8;;"
URL_END = "\033]8;;\a"

STATUS_COLORS = {
    "created": "",
    "running": Fore.BLUE,
    "success": Fore.GREEN,
    "failed": Fore.RED,
    "canceled": Fore.MAGENTA,
    "manual": "",
    "pending": "",
    "skipped": "",
}

# TODO: This hardcoded list should be replaced by querying the pipeline's
# dependency graph to see which jobs the target jobs need
DEPENDENCIES = [
    "debian/x86_build-base",
    "debian/x86_build",
    "debian/x86_test-base",
    "debian/x86_test-gl",
    "debian/arm_build",
    "debian/arm_test",
    "kernel+rootfs_amd64",
    "kernel+rootfs_arm64",
    "kernel+rootfs_armhf",
    "debian-testing",
    "debian-arm64",
]

COMPLETED_STATUSES = ["success", "failed"]


def get_gitlab_project(glab, name: str):
    """Finds a specified gitlab project for given user"""
    glab.auth()
    username = glab.user.username
    return glab.projects.get(f"{username}/mesa")


def wait_for_pipeline(project, sha: str):
    """await until pipeline appears in Gitlab"""
    print("⏲ for the pipeline to appear..", end="")
    while True:
        pipelines = project.pipelines.list(sha=sha)
        if pipelines:
            print("", flush=True)
            return pipelines[0]
        print("", end=".", flush=True)
        time.sleep(1)


def print_job_status(job) -> None:
    """It prints a nice, colored job status with a link to the job."""
    if job.status == "canceled":
        return

    print(
        STATUS_COLORS[job.status]
        + "🞋 job "
        + URL_START
        + f"{job.web_url}\a{job.name}"
        + URL_END
        + f" :: {job.status}"
        + Style.RESET_ALL
    )


def print_job_status_change(job) -> None:
    """It reports job status changes."""
    if job.status == "canceled":
        return

    print(
        STATUS_COLORS[job.status]
        + "🗘 job "
        + URL_START
        + f"{job.web_url}\a{job.name}"
        + URL_END
        + f" has new status: {job.status}"
        + Style.RESET_ALL
    )


def pretty_wait(sec: int) -> None:
    """shows progressbar in dots"""
    for val in range(sec, 0, -1):
        print(f"⏲  {val} seconds", end="\r")
        time.sleep(1)


def monitor_pipeline(
    project, pipeline, target_job: Optional[str], dependencies, force_manual: bool
) -> tuple[Optional[int], Optional[int]]:
    """Monitors pipeline and delegate canceling jobs"""
    statuses = {}
    target_statuses = {}

    if not dependencies:
        dependencies = []
    dependencies.extend(DEPENDENCIES)

    if target_job:
        target_jobs_regex = re.compile(target_job.strip())

    while True:
        to_cancel = []
        for job in pipeline.jobs.list(all=True, sort="desc"):
            # target jobs
            if target_job and target_jobs_regex.match(job.name):
                if force_manual and job.status == "manual":
                    enable_job(project, job, True)

                if (job.id not in target_statuses) or (
                    job.status not in target_statuses[job.id]
                ):
                    print_job_status_change(job)
                    target_statuses[job.id] = job.status
                else:
                    print_job_status(job)

                continue

            # all jobs
            if (job.id not in statuses) or (job.status not in statuses[job.id]):
                print_job_status_change(job)
                statuses[job.id] = job.status

            # dependencies and cancelling the rest
            if job.name in dependencies:
                if job.status == "manual":
                    enable_job(project, job, False)

            elif target_job and job.status not in [
                "canceled",
                "success",
                "failed",
                "skipped",
            ]:
                to_cancel.append(job)

        if target_job:
            cancel_jobs(project, to_cancel)

        print("---------------------------------", flush=False)

        if len(target_statuses) == 1 and {"running"}.intersection(
            target_statuses.values()
        ):
            return next(iter(target_statuses)), None

        if {"failed", "canceled"}.intersection(target_statuses.values()):
            return None, 1

        if {"success", "manual"}.issuperset(target_statuses.values()):
            return None, 0

        pretty_wait(REFRESH_WAIT_JOBS)


def enable_job(project, job, target: bool) -> None:
    """enable manual job"""
    pjob = project.jobs.get(job.id, lazy=True)
    pjob.play()
    if target:
        jtype = "🞋 "
    else:
        jtype = "(dependency)"
    print(Fore.MAGENTA + f"{jtype} job {job.name} manually enabled" + Style.RESET_ALL)


def cancel_job(project, job) -> None:
    """Cancel GitLab job"""
    pjob = project.jobs.get(job.id, lazy=True)
    pjob.cancel()
    print(f"♲ {job.name}")


def cancel_jobs(project, to_cancel) -> None:
    """Cancel unwanted GitLab jobs"""
    if not to_cancel:
        return

    with ThreadPoolExecutor(max_workers=6) as exe:
        part = partial(cancel_job, project)
        exe.map(part, to_cancel)


def print_log(project, job_id) -> None:
    """Print job log into output"""
    printed_lines = 0
    while True:
        job = project.jobs.get(job_id)

        # GitLab's REST API doesn't offer pagination for logs, so we have to refetch it all
        lines = job.trace().decode("unicode_escape").splitlines()
        for line in lines[printed_lines:]:
            print(line)
        printed_lines = len(lines)

        if job.status in COMPLETED_STATUSES:
            print(Fore.GREEN + f"Job finished: {job.web_url}" + Style.RESET_ALL)
            return
        pretty_wait(REFRESH_WAIT_LOG)


def parse_args() -> None:
    """Parse args"""
    parser = argparse.ArgumentParser(
        description="Tool to trigger a subset of container jobs "
        + "and monitor the progress of a test job",
        epilog="Example: mesa-monitor.py --rev $(git rev-parse HEAD) "
        + '--target ".*traces" ',
    )
    parser.add_argument("--target", metavar="target-job", help="Target job")
    parser.add_argument("--deps", nargs="+", help="Job dependencies")
    parser.add_argument(
        "--rev", metavar="revision", help="repository git revision", required=True
    )
    parser.add_argument(
        "--token",
        metavar="token",
        help="force GitLab token, otherwise it's read from ~/.config/gitlab-token",
    )
    parser.add_argument(
        "--force-manual", action="store_true", help="Force jobs marked as manual"
    )
    return parser.parse_args()


def read_token(token_arg: Optional[str]) -> str:
    """pick token from args or file"""
    if token_arg:
        return token_arg
    return (
        open(os.path.expanduser("~/.config/gitlab-token"), encoding="utf-8")
        .readline()
        .rstrip()
    )


if __name__ == "__main__":
    try:
        t_start = time.perf_counter()

        args = parse_args()

        token = read_token(args.token)

        gl = gitlab.Gitlab(url="https://gitlab.freedesktop.org", private_token=token)

        cur_project = get_gitlab_project(gl, "mesa")

        print(f"Revision: {args.rev}")
        pipe = wait_for_pipeline(cur_project, args.rev)
        print(f"Pipeline: {pipe.web_url}")
        if args.target:
            print("🞋 job: " + Fore.BLUE + args.target + Style.RESET_ALL)
        print(f"Extra dependencies: {args.deps}")
        target_job_id, ret = monitor_pipeline(
            cur_project, pipe, args.target, args.deps, args.force_manual
        )

        if target_job_id:
            print_log(cur_project, target_job_id)

        t_end = time.perf_counter()
        spend_minutes = (t_end - t_start) / 60
        print(f"⏲ Duration of script execution: {spend_minutes:0.1f} minutes")

        sys.exit(ret)
    except KeyboardInterrupt:
        sys.exit(1)
