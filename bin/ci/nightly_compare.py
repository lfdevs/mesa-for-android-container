#!/usr/bin/env python3
# Copyright © 2020 - 2024 Collabora Ltd.
# Authors:
#   David Heidelberg <david.heidelberg@collabora.com>
#   Sergi Blanch Torne <sergi.blanch.torne@collabora.com>
# SPDX-License-Identifier: MIT

"""
Compare the two latest scheduled pipelines and provide information
about the jobs you're interested in.
"""

import argparse
import csv
import re
import requests
import io
from tabulate import tabulate

import gitlab
from gitlab_common import read_token
from rich import print


MARGE_BOT_USER_ID = 9716


def print_failures_csv(id):
    url = "https://gitlab.freedesktop.org/mesa/mesa"\
        f"/-/jobs/{id}/artifacts/raw/results/failures.csv"
    missing: int = 0
    MAX_MISS: int = 20
    try:
        response = requests.get(url)
        response.raise_for_status()
        csv_content = io.StringIO(response.text)
        csv_reader = csv.reader(csv_content)
        data = list(csv_reader)

        for line in data[:]:
            if line[1] == "UnexpectedImprovement(Pass)":
                line[1] = f"[green]{line[1]}[/green]"
            elif line[1] == "UnexpectedImprovement(Fail)":
                line[1] = f"[yellow]{line[1]}[/yellow]"
            elif line[1] == "Crash" or line[1] == "Fail":
                line[1] = f" [red]{line[1]}[/red]"
            elif line[1] == "Missing":
                if missing > MAX_MISS:
                    data.remove(line)
                    continue
                missing += 1
                line[1] = f"[yellow]{line[1]}[/yellow]"
            elif line[1] == "Fail":
                line[1] = f"[red]{line[1]}[/red]"
            else:
                line[1] = f"[white]{line[1]}[/white]"

        if missing > MAX_MISS:
            data.append(
                [
                    f"[red]... more than {MAX_MISS} missing tests, "
                    "something crashed?[/red]",
                    "[red]Missing[/red]"
                ]
            )
        headers = [f"Test{"":<75}", "Result"]
        print(tabulate(data, headers, tablefmt="plain"))
    except Exception:
        pass


def job_failed_before(old_jobs, job):
    for old_job in old_jobs:
        if job.name == old_job.name:
            return old_job


def parse_args() -> None:
    """Parse args"""
    parser = argparse.ArgumentParser(
        description="Tool to show merge requests assigned to the marge-bot",
    )
    parser.add_argument(
        "--target",
        metavar="target-job",
        help="Target job regex. For multiple targets, pass multiple values, "
        "eg. `--target foo bar`.",
        required=False,
        nargs=argparse.ONE_OR_MORE,
    )
    parser.add_argument(
        "--token",
        metavar="token",
        help="force GitLab token, "
        "otherwise it's read from ~/.config/gitlab-token",
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    token = read_token(args.token)
    gl = gitlab.Gitlab(
        url="https://gitlab.freedesktop.org",
        private_token=token,
    )

    project = gl.projects.get("mesa/mesa")

    print(
        "[link=https://gitlab.freedesktop.org/mesa/mesa/-/pipelines?"
        "page=1&scope=all&source=schedule]Scheduled pipelines overview[/link]"
    )
    pipelines = project.pipelines.list(
        source="schedule",
        ordered_by="created_at",
        sort="desc",
        page=1,
        per_page=2,
    )
    print(
        "Old pipeline:"
        f" {pipelines[1].created_at}"
        f"\t[link={pipelines[1].web_url}]{pipelines[1].status}[/link]"
        f"\t{pipelines[1].sha}"
    )
    print(
        "New pipeline:"
        f" {pipelines[0].created_at}"
        f"\t[link={pipelines[0].web_url}]{pipelines[0].status}[/link]"
        f"\t{pipelines[0].sha}"
    )
    print(
        "\nWebUI visual compare: "
        "https://gitlab.freedesktop.org/mesa/mesa/-/compare/"
        f"{pipelines[1].sha}...{pipelines[0].sha}\n"
    )

    # regex part
    if args.target:
        target = "|".join(args.target)
        target = target.strip()
        print(f"🞋 jobs: [blue]{target}[/blue]")

        target = f"({target})" + r"( \d+/\d+)?"
    else:
        target = ".*"

    target_jobs_regex: re.Pattern = re.compile(target)

    old_failed_jobs = []
    for job in pipelines[1].jobs.list(all=True):
        if (
            job.status != "failed"
            or target_jobs_regex
            and not target_jobs_regex.fullmatch(job.name)
        ):
            continue
        old_failed_jobs.append(job)

    job_failed = False
    for job in pipelines[0].jobs.list(all=True):
        if (
            job.status != "failed"
            or target_jobs_regex
            and not target_jobs_regex.fullmatch(job.name)
        ):
            continue

        job_failed = True

        previously_failed_job = job_failed_before(old_failed_jobs, job)
        if previously_failed_job:
            print(
                f"[yellow]"
                f" :: [link={job.web_url}]{job.name}[/link][/yellow]"
                f"[magenta]"
                f" [link={previously_failed_job.web_url}](previous run)[/link]"
            )
        else:
            print(
                f"[red]:: [link={job.web_url}]{job.name}[/link]"
            )
        print_failures_csv(job.id)

    if not job_failed:
        exit(0)

    print("Commits between nightly pipelines:")
    commit = project.commits.get(pipelines[0].sha)
    while True:
        print(
            f"{commit.id}  [link={commit.web_url}]{commit.title}[/link]"
        )
        if commit.id == pipelines[1].sha:
            break
        commit = project.commits.get(commit.parent_ids[0])
