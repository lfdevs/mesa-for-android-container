#!/usr/bin/env python3
# Copyright © 2020 - 2022 Collabora Ltd.
# Authors:
#   Tomeu Vizoso <tomeu.vizoso@collabora.com>
#   David Heidelberg <david.heidelberg@collabora.com>
#
# For the dependencies, see the requirements.txt
# SPDX-License-Identifier: MIT

"""
Helper script to restrict running only required CI jobs
and show the job(s) logs.
"""

import argparse
import re
import sys
import time
from collections import defaultdict, Counter
from concurrent.futures import ThreadPoolExecutor
from functools import partial
from itertools import chain
from subprocess import check_output, CalledProcessError
from typing import Callable, Dict, TYPE_CHECKING, Iterable, Literal, Optional, Tuple, cast

import gitlab
import gitlab.v4.objects
from gitlab_common import (
    GITLAB_URL,
    TOKEN_DIR,
    get_gitlab_pipeline_from_url,
    get_gitlab_project,
    get_token_from_default_dir,
    pretty_duration,
    read_token,
    wait_for_pipeline,
)
from gitlab_gql import GitlabGQL, create_job_needs_dag, filter_dag, print_dag, print_formatted_list
from rich.console import Console

if TYPE_CHECKING:
    from gitlab_gql import Dag

REFRESH_WAIT_LOG = 10
REFRESH_WAIT_JOBS = 6
MAX_ENABLE_JOB_ATTEMPTS = 3

STATUS_COLORS = {
    "created": "",
    "running": "[blue]",
    "success": "[green]",
    "failed": "[red]",
    "canceled": "[magenta]",
    "canceling": "[magenta]",
    "manual": "",
    "pending": "",
    "skipped": "",
}

COMPLETED_STATUSES = frozenset({"success", "failed"})
RUNNING_STATUSES = frozenset({"created", "pending", "running"})

console = Console(highlight=False)
print = console.print


def print_job_status(
    job: gitlab.v4.objects.ProjectPipelineJob,
    new_status: bool = False,
) -> None:
    """It prints a nice, colored job status with a link to the job."""
    if job.status in {"canceled", "canceling"}:
        return

    if new_status and job.status == "created":
        return

    global type_field_pad
    global name_field_pad
    jtype = "🞋 job"
    job_name = job.name
    type_field_pad = len(jtype) if len(jtype) > type_field_pad else type_field_pad
    name_field_pad = len(job_name) if len(job_name) > name_field_pad else name_field_pad

    duration = job_duration(job)

    print(
        f"{STATUS_COLORS[job.status]}"
        f"{jtype:{type_field_pad}} "  # U+1F78B Round target
        f"{link2print(job.web_url, job.name, name_field_pad)} " +
        (f" has new status: {job.status}" if new_status else f" {job.status}") +
        (f" ({pretty_duration(duration)})" if job.started_at else "")
    )


def job_duration(job: gitlab.v4.objects.ProjectPipelineJob) -> float:
    """
    Given a job, report the time lapsed in execution.
    :param job: Pipeline job
    :return: Current time in execution
    """
    if job.duration:
        return job.duration
    elif job.started_at:
        # Convert both times to UTC timestamps for consistent comparison
        current_time = time.time()
        start_time = job.started_at.timestamp()
        return current_time - start_time
    return 0.0


def pretty_wait(sec: int) -> None:
    """shows progressbar in dots"""
    for val in range(sec, 0, -1):
        print(f"⏲  {val:2d} seconds", end="\r")  # U+23F2 Timer clock
        time.sleep(1)


def run_target_job(
    job: gitlab.v4.objects.ProjectPipelineJob,
    enable_job_fn: Callable,
    stress: int,
    execution_times: dict,
    target_statuses: dict,
) -> None:
    execution_times[job.name][job.id] = (job_duration(job), job.status, job.web_url)
    if stress and job.status in COMPLETED_STATUSES:
        if (
            stress < 0
            or len(execution_times[job.name]) < stress
        ):
            enable_job_fn(job=job, action_type="retry")
            # Wait for the next loop to get the updated job object
            return
    else:
        enable_job_fn(job=job, action_type="target")

    print_job_status(job, job.status not in target_statuses[job.name])
    target_statuses[job.name] = job.status


def monitor_pipeline(
    project: gitlab.v4.objects.Project,
    pipeline: gitlab.v4.objects.ProjectPipeline,
    job_filter: callable,
    dependencies: set[str],
    stress: int,
) -> tuple[Optional[int], Optional[int], Dict[str, Dict[int, Tuple[float, str, str]]]]:
    """Monitors pipeline and delegate canceling jobs"""
    statuses: dict[str, str] = defaultdict(str)
    target_statuses: dict[str, str] = defaultdict(str)
    execution_times: dict[str, dict[str, tuple[float, str, str]]] = defaultdict(lambda: defaultdict(tuple))
    target_id: int = -1
    global type_field_pad
    type_field_pad = 0
    global name_field_pad
    name_field_pad = len(max(dependencies, key=len))+2
    # In a running pipeline, we can skip following job traces that are in these statuses.
    skip_follow_statuses: frozenset[str] = (COMPLETED_STATUSES)

    # Pre-populate the stress status counter for already completed target jobs.
    if stress:
        # When stress test, it is necessary to collect this information before start.
        for job in pipeline.jobs.list(all=True, include_retried=True):
            if job_filter(
                job_name=job.name,
                job_stage=job.stage,
                job_tags=job.tag_list,
            ) and job.status in COMPLETED_STATUSES:
                execution_times[job.name][job.id] = (job_duration(job), job.status, job.web_url)

    # jobs_waiting is a list of job names that are waiting for status update.
    # It occurs when a job that we want to run depends on another job that is not yet finished.
    jobs_waiting = []
    # Dictionary to track the number of attempts made for each job
    enable_attempts: dict[int, int] = {}
    # FIXME: This function has too many parameters, consider refactoring.
    enable_job_fn = partial(
        enable_job,
        project=project,
        enable_attempts=enable_attempts,
        jobs_waiting=jobs_waiting,
    )
    while True:
        deps_failed = []
        to_cancel = []
        jobs_waiting.clear()
        for job in sorted(pipeline.jobs.list(all=True), key=lambda j: j.name):
            job = cast(gitlab.v4.objects.ProjectPipelineJob, job)
            if job_filter(
                job_name=job.name,
                job_stage=job.stage,
                job_tags=job.tag_list,
            ):
                run_target_job(
                    job,
                    enable_job_fn,
                    stress,
                    execution_times,
                    target_statuses
                )
                target_id = job.id
                continue
            # all other non-target jobs
            if job.status != statuses[job.name]:
                print_job_status(job, True)
                statuses[job.name] = job.status

            # run dependencies and cancel the rest
            if job.name in dependencies:
                if not enable_job_fn(job=job, action_type="dep"):
                    # Wait for the next loop to get the updated job object
                    continue
                if job.status == "failed":
                    deps_failed.append(job.name)
            else:
                to_cancel.append(job)

        cancel_jobs(project, to_cancel)

        if stress:
            enough = True
            status_counters = {
                name: Counter(info[1] for info in runs.values())
                for name, runs in execution_times.items()
            }
            for job_name, counter in sorted(status_counters.items()):
                n_succeed = counter.get("success", 0)
                n_failed = counter.get("failed", 0)
                n_total_completed = n_succeed + n_failed
                n_total_seen = len(execution_times[job_name])
                print(
                    f"* {job_name:{name_field_pad}} succ: {n_succeed}; "
                    f"fail: {n_failed}; "
                    f"total: {n_total_seen} of {stress}",
                )
                if stress < 0 or n_total_completed < stress:
                    enough = False

            if not enough:
                pretty_wait(REFRESH_WAIT_JOBS)
                continue

        if jobs_waiting:
            print(f"[yellow]Waiting for jobs to update status:")
            print_formatted_list(jobs_waiting, indentation=8, color="[yellow]")
            pretty_wait(REFRESH_WAIT_JOBS)
            continue

        if (
            stress in [0, 1]
            and len(target_statuses) == 1
            and RUNNING_STATUSES.intersection(target_statuses.values())
        ):
            return target_id, None, execution_times

        if (
            {"failed"}.intersection(target_statuses.values())
            and not RUNNING_STATUSES.intersection(target_statuses.values())
        ):
            return None, 1, execution_times

        if (
            {"skipped"}.intersection(target_statuses.values())
            and not RUNNING_STATUSES.intersection(target_statuses.values())
        ):
            print(
                f"[red]Target in skipped state, aborting. Failed dependencies:{deps_failed}"
            )
            return None, 1, execution_times

        if skip_follow_statuses.issuperset(target_statuses.values()):
            return None, 0, execution_times

        pretty_wait(REFRESH_WAIT_JOBS)


def enable_job(
    project: gitlab.v4.objects.Project,
    job: gitlab.v4.objects.ProjectPipelineJob,
    enable_attempts: dict[int, int],
    action_type: Literal["target", "dep", "retry"],
    jobs_waiting: list[str] = list,
) -> bool:
    """
    Enable a job to run.
    :param project: The GitLab project.
    :param job: The job to enable.
    :param enable_attempts: A dictionary to track the number of attempts made for each job.
    :param action_type: The type of action to perform.
    :param jobs_waiting:
    :return: True if the job was enabled, False otherwise.
    """
    # We want to run this job, but it is not ready to run yet, so let's try again in the next
    # iteration.
    if job.status == "created":
        jobs_waiting.append(job.name)
        return False

    if (
        (job.status in COMPLETED_STATUSES and action_type != "retry")
        or job.status in {"skipped"} | RUNNING_STATUSES
    ):
        return False

    # Get current attempt number
    attempt_count = enable_attempts.get(job.id, 0)
    # Check if we've exceeded max attempts to avoid infinite loop
    if attempt_count >= MAX_ENABLE_JOB_ATTEMPTS:
        raise RuntimeError(
            f"Maximum enabling attempts ({MAX_ENABLE_JOB_ATTEMPTS}) reached for job {job.name} "
            f"({link2print(job.web_url, job.id)}). Giving up."
        )
    enable_attempts[job.id] = attempt_count + 1

    pjob = project.jobs.get(job.id, lazy=True)

    if job.status in {"success", "failed", "canceled", "canceling"}:
        try:
            pjob.retry()
        except Exception as e:
            print(f"Error retrying job {job.name}: {e}")
            return False
    else:
        try:
            pjob.play()
        except Exception as e:
            print(f"Error playing job {job.name}: {e}")
            return False

    if action_type == "target":
        jtype = "🞋 target"  # U+1F78B Round target
    elif action_type == "retry":
        jtype = "↻ retrying"  # U+21BB Clockwise open circle arrow
    else:
        jtype = "↪ dependency"  # U+21AA Left Arrow Curving Right

    global type_field_pad
    global name_field_pad
    job_name = job.name
    type_field_pad = len(jtype) if len(jtype) > type_field_pad else type_field_pad
    name_field_pad = len(job_name) if len(job_name) > name_field_pad else name_field_pad
    print(
        f"[magenta]{jtype:{type_field_pad}} {job.name:{name_field_pad}} manually enabled"
    )

    return True


def cancel_job(
    project: gitlab.v4.objects.Project,
    pipeline_job: gitlab.v4.objects.ProjectPipelineJob
) -> Optional[gitlab.v4.objects.ProjectPipelineJob]:
    """
    Cancel GitLab job
    :param project: project from the pipeline job comes from
    :param pipeline_job: job made from the pipeline list
    :return the job object when cancel was called
    """
    if pipeline_job.status not in RUNNING_STATUSES:
        return
    try:
        project_job = project.jobs.get(pipeline_job.id, lazy=True)
        project_job.cancel()
    except (gitlab.GitlabCancelError, gitlab.GitlabGetError):
        # If the job failed to cancel, it will be retried in the monitor_pipeline() next iteration
        return
    return pipeline_job


def cancel_jobs(
    project: gitlab.v4.objects.Project,
    to_cancel: list[gitlab.v4.objects.ProjectPipelineJob]
) -> None:
    """
    Cancel unwanted GitLab jobs
    :param project: project from where the pipeline comes
    :param to_cancel: list of jobs to be cancelled
    """
    if not to_cancel:
        return

    with ThreadPoolExecutor(max_workers=6) as exe:
        part = partial(cancel_job, project)
        maybe_cancelled_job = exe.map(part, to_cancel)
        cancelled_jobs = [f"🗙 {job.name}" for job in maybe_cancelled_job if job]  # U+1F5D9 Cancellation X

    # The cancelled jobs are printed without a newline
    if len(cancelled_jobs):
        print(f"Cancelled {len(cancelled_jobs)} jobs:")
        print_formatted_list(cancelled_jobs, indentation=8)


def print_log(
    project: gitlab.v4.objects.Project,
    job_id: int
) -> None:
    """Print job log into output"""
    printed_lines = 0
    while True:
        job = project.jobs.get(job_id)

        # GitLab's REST API doesn't offer pagination for logs, so we have to refetch it all
        lines = job.trace().decode().splitlines()
        for line in lines[printed_lines:]:
            print(line)
        printed_lines = len(lines)

        if job.status in COMPLETED_STATUSES:
            print(f"[green]Job finished: {job.web_url}")
            return
        pretty_wait(REFRESH_WAIT_LOG)


def parse_args() -> argparse.Namespace:
    """Parse args"""
    parser = argparse.ArgumentParser(
        description="Tool to trigger a subset of container jobs "
        + "and monitor the progress of a test job",
        epilog="Example: mesa-monitor.py --rev $(git rev-parse HEAD) "
        + '--target ".*traces" ',
    )
    parser.add_argument(
        "--server",
        metavar="gitlab-server",
        type=str,
        default=GITLAB_URL,
        help=f"Specify the GitLab server work with (Default: {GITLAB_URL})",
    )
    parser.add_argument(
        "--target",
        metavar="target-job",
        help="Target job regex. For multiple targets, pass multiple values, "
             "eg. `--target foo bar`. Only jobs in the target stage(s) "
             "supplied, and their dependencies, will be considered.",
        required=True,
        nargs=argparse.ONE_OR_MORE,
    )
    parser.add_argument(
        "--include-stage",
        metavar="include-stage",
        help="Job stages to include when searching for target jobs. "
             "For multiple targets, pass multiple values, eg. "
             "`--include-stage foo bar`.",
        default=[".*"],
        nargs=argparse.ONE_OR_MORE,
    )
    parser.add_argument(
        "--exclude-stage",
        metavar="exclude-stage",
        help="Job stages to exclude when searching for target jobs. "
             "For multiple targets, pass multiple values, eg. "
             "`--exclude-stage foo bar`. By default, performance and "
             "nightly jobs are excluded; pass --exclude-stage '' to "
             "include them for consideration.",
        default=["performance", ".*-postmerge", ".*-nightly"],
        nargs=argparse.ONE_OR_MORE,
    )
    parser.add_argument(
        "--job-tags",
        metavar="job-tags",
        help="Job tags to require when searching for target jobs. If multiple "
             "values are passed, eg. `--job-tags 'foo.*' 'bar'`, the job will "
             "need to have a tag matching `foo.*` *and* a tag matching `bar` "
             "to qualify. Passing `--job-tags '.*'` makes sure the job has "
             "a tag defined, while not passing `--job-tags` also allows "
             "untagged jobs.",
        default=[],
        nargs=argparse.ONE_OR_MORE,
    )
    parser.add_argument(
        "--token",
        metavar="token",
        type=str,
        default=get_token_from_default_dir(),
        help="Use the provided GitLab token (with `api` scope) or token file, "
             f"otherwise it's read from {TOKEN_DIR / 'gitlab-token'}",
    )
    parser.add_argument(
        "--force-manual", action="store_true",
        help="Deprecated argument; manual jobs are always force-enabled"
    )
    parser.add_argument(
        "--stress",
        metavar="n",
        type=int,
        default=0,
        help="Stresstest job(s). Specify the number of times to rerun the selected jobs, "
             "or use -1 for indefinite. Defaults to 0. If jobs have already been executed, "
             "this will ensure the total run count respects the specified number.",
    )
    parser.add_argument(
        "--project",
        metavar="name",
        type=str,
        default="mesa",
        help="GitLab project in the format <user>/<project> or just <project>",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Exit after printing target jobs and dependencies",
    )

    mutex_group1 = parser.add_mutually_exclusive_group()
    mutex_group1.add_argument(
        "--rev",
        metavar="id",
        type=str,
        default="HEAD",
        help="Repository git commit-ish, tag or branch name (default: HEAD)",
    )
    mutex_group1.add_argument(
        "--pipeline-url",
        metavar="url",
        type=str,
        help="URL of the pipeline to use, instead of auto-detecting it.",
    )
    mutex_group1.add_argument(
        "--mr",
        metavar="id",
        type=int,
        help="ID of a merge request; the latest pipeline in that MR will be used.",
    )

    args = parser.parse_args()

    # argparse doesn't support groups inside add_mutually_exclusive_group(),
    # which means we can't just put `--project` and `--rev` in a group together,
    # we have to do this by heand instead.
    if args.pipeline_url and args.project != parser.get_default("project"):
        # weird phrasing but it's the error add_mutually_exclusive_group() gives
        parser.error("argument --project: not allowed with argument --pipeline-url")

    return args


def print_detected_jobs(
    target_dep_dag: "Dag",
    dependency_jobs: Iterable[str],
    target_jobs: Iterable[str],
) -> None:
    def print_job_set(color: str, kind: str, job_set: Iterable[str]):
        job_list = list(job_set)
        print(f"{color}Running {len(job_list)} {kind} jobs:")
        print_formatted_list(job_list, indentation=8, color=color)

    print("[yellow]Detected target job and its dependencies:")
    print_dag(target_dep_dag, indentation=8, color="[yellow]")
    print_job_set("[magenta]", "dependency", dependency_jobs)
    print_job_set("[blue]", "target", target_jobs)


def find_dependencies(
    server: str,
    token: str | None,
    job_filter: callable,
    project_path: str,
    iid: int
) -> set[str]:
    """
    Find the dependencies of the target jobs in a GitLab pipeline.

    This function uses the GitLab GraphQL API to fetch the job dependency graph
    of a pipeline, filters the graph to only include the target jobs and their
    dependencies, and returns the names of these jobs.

    Args:
        server (str): The url to the GitLab server.
        token (str | None): The GitLab API token. If None, the API is accessed without
                            authentication.
        target_jobs_regex (re.Pattern): A regex pattern to match the names of the target jobs.
        project_path (str): The path of the GitLab project.
        iid (int): The internal ID of the pipeline.

    Returns:
        set[str]: A set of the names of the target jobs and their dependencies.

    Raises:
        SystemExit: If no target jobs are found in the pipeline.
    """
    gql_instance = GitlabGQL(
        url=f"{server}/api/graphql",
        token=token
    )
    dag = create_job_needs_dag(
        gql_instance, {"projectPath": project_path.path_with_namespace, "iid": iid}
    )

    target_dep_dag = filter_dag(dag, job_filter)
    if not target_dep_dag:
        print("[red]The job(s) were not found in the pipeline.")
        sys.exit(1)

    dependency_jobs = set(chain.from_iterable(d["needs"] for d in target_dep_dag.values()))
    target_jobs = set(target_dep_dag.keys())
    print_detected_jobs(target_dep_dag, dependency_jobs, target_jobs)
    return target_jobs.union(dependency_jobs)


def print_monitor_summary(
    execution_collection: Dict[str, Dict[int, Tuple[float, str, str]]],
    t_start: float,
) -> None:
    """Summary of the test execution"""
    t_end = time.perf_counter()
    spend_minutes = (t_end - t_start) / 60
    print(f"⏲ Duration of script execution: {spend_minutes:0.1f} minutes")  # U+23F2 Timer clock
    if len(execution_collection) == 0:
        return
    print(f"⏲ Jobs execution times:")  # U+23F2 Timer clock
    job_names = list(execution_collection.keys())
    job_names.sort()
    name_field_pad = len(max(job_names, key=len)) + 2
    for name in job_names:
        job_executions = execution_collection[name]
        job_times = ', '.join([__job_duration_record(job_execution)
                               for job_execution in sorted(job_executions.items())])
        print(f"* {name:{name_field_pad}}: ({len(job_executions)}) {job_times}")


def __job_duration_record(dict_item: tuple) -> str:
    """
    Format each pair of job and its duration.
    :param job_execution: item of execution_collection[name][idn]: Dict[int, Tuple[float, str, str]]
    """
    job_id = f"{dict_item[0]}"  # dictionary key
    job_duration, job_status, job_url = dict_item[1]  # dictionary value, the tuple
    return (
        f"{STATUS_COLORS[job_status]}"
        f"{link2print(job_url, job_id)}: {pretty_duration(job_duration):>8}"
    )


def link2print(url: str, text: str, text_pad: int = 0) -> str:
    text = str(text)
    text_pad = len(text) if text_pad < 1 else text_pad
    return f"[link={url}]{text:{text_pad}}[/link]"


def main() -> None:
    try:
        t_start = time.perf_counter()

        args = parse_args()

        token = read_token(args.token)

        gl = gitlab.Gitlab(url=args.server,
                           private_token=token,
                           retry_transient_errors=True)

        REV: str = args.rev

        if args.pipeline_url:
            pipe, cur_project = get_gitlab_pipeline_from_url(gl, args.pipeline_url)
            REV = pipe.sha
        else:
            mesa_project = gl.projects.get("mesa/mesa")
            projects = [mesa_project]
            if args.mr:
                REV = mesa_project.mergerequests.get(args.mr).sha
            else:
                REV = check_output(['git', 'rev-parse', REV]).decode('ascii').strip()

                if args.rev == 'HEAD':
                    try:
                        branch_name = check_output([
                            'git', 'symbolic-ref', '-q', 'HEAD',
                        ]).decode('ascii').strip()
                    except CalledProcessError:
                        branch_name = ""

                    # Ignore detached heads
                    if branch_name:
                        tracked_remote = check_output([
                            'git', 'for-each-ref', '--format=%(upstream)',
                            branch_name,
                        ]).decode('ascii').strip()

                        # Ignore local branches that do not track any remote
                        if tracked_remote:
                            remote_rev = check_output([
                                'git', 'rev-parse', tracked_remote,
                            ]).decode('ascii').strip()

                            if REV != remote_rev:
                                print(
                                    f"Local HEAD commit {REV[:10]} is different than "
                                    f"tracked remote HEAD commit {remote_rev[:10]}"
                                )
                                print("Did you forget to `git push` ?")

                projects.append(get_gitlab_project(gl, args.project))
            (pipe, cur_project) = wait_for_pipeline(projects, REV)

        print(f"Revision: {REV}")
        print(f"Pipeline: {pipe.web_url}")

        target = '|'.join(args.target)
        target = target.strip()

        print(f"🞋 target job: [blue]{target}")  # U+1F78B Round target

        # Implicitly include `parallel:` jobs
        target = f'({target})' + r'( \d+/\d+)?'

        target_jobs_regex = re.compile(target)

        include_stage = '|'.join(args.include_stage)
        include_stage = include_stage.strip()

        print(f"🞋 target from stages: [blue]{include_stage}")  # U+1F78B Round target

        include_stage_regex = re.compile(include_stage)

        exclude_stage = '|'.join(args.exclude_stage)
        exclude_stage = exclude_stage.strip()

        print(f"🞋 target excluding stages: [blue]{exclude_stage}")  # U+1F78B Round target

        exclude_stage_regex = re.compile(exclude_stage)

        print(f"🞋 target jobs with tags: [blue]{str(args.job_tags)}")  # U+1F78B Round target
        job_tags_regexes = [re.compile(job_tag) for job_tag in args.job_tags]

        def job_filter(
            job_name: str,
            job_stage: str,
            job_tags: set[str],
        ) -> bool:
            """
            Apply user-specified filters to a job, and return whether the
            filters allow that job (True) or not (False).
            """
            if not target_jobs_regex.fullmatch(job_name):
                return False
            if not include_stage_regex.fullmatch(job_stage):
                return False
            if exclude_stage_regex.fullmatch(job_stage):
                return False
            if not all(
                any(job_tags_regex.fullmatch(tag) for tag in job_tags)
                for job_tags_regex in job_tags_regexes
            ):
                return False
            return True

        deps = find_dependencies(
            server=args.server,
            token=token,
            job_filter=job_filter,
            iid=pipe.iid,
            project_path=cur_project
        )

        if args.dry_run:
            sys.exit(0)

        target_job_id, ret, exec_t = monitor_pipeline(
            cur_project,
            pipe,
            job_filter,
            deps,
            args.stress
        )

        if target_job_id:
            print_log(cur_project, target_job_id)

        print_monitor_summary(exec_t, t_start)

        sys.exit(ret)
    except KeyboardInterrupt:
        sys.exit(1)


if __name__ == "__main__":
    main()
