#!/usr/bin/env python3
# Copyright © 2020 - 2022 Collabora Ltd.
# Authors:
#   Tomeu Vizoso <tomeu.vizoso@collabora.com>
#   David Heidelberg <david.heidelberg@collabora.com>
#   Guilherme Gallo <guilherme.gallo@collabora.com>
#
# SPDX-License-Identifier: MIT
'''Shared functions between the scripts.'''

import logging
import os
import re
import time
from functools import cache
from pathlib import Path
from typing import Optional

from gitlab import Gitlab
from gitlab.v4.objects.projects import Project
from gitlab.v4.objects.pipelines import ProjectPipeline

GITLAB_URL = "https://gitlab.freedesktop.org"
TOKEN_DIR = Path(os.environ.get("XDG_CONFIG_HOME", "")
                 if os.environ.get("XDG_CONFIG_HOME", None)
                 else Path.home() / ".config")

# Known GitLab token prefixes: https://docs.gitlab.com/security/tokens/#token-prefixes
TOKEN_PREFIXES: dict[str, str] = {
    "Personal access token": "glpat-",
    "OAuth Application Secret": "gloas-",
    "Deploy token": "gldt-",
    "Runner authentication token": "glrt-",
    "CI/CD Job token": "glcbt-",
    "Trigger token": "glptt-",
    "Feed token": "glft-",
    "Incoming mail token": "glimt-",
    "GitLab Agent for Kubernetes token": "glagent-",
    "SCIM Tokens": "glsoat-",
}


@cache
def print_once(*args, **kwargs):
    """Print without spamming the output"""
    print(*args, **kwargs)


def pretty_duration(seconds: int | float) -> str:
    """Pretty print duration"""
    seconds = int(seconds)
    hours, rem = divmod(seconds, 3600)
    minutes, seconds = divmod(rem, 60)
    if hours:
        return f"{hours:0.0f}h{minutes:02.0f}m{seconds:02.0f}s"
    if minutes:
        return f"{minutes:0.0f}m{seconds:02.0f}s"
    return f"{seconds:0.0f}s"


def get_server_and_project_from_url(pipeline_url: str) -> tuple[str]:
    """
    Extract the string of the server and path that means the project with namespace
    from a url that points to a pipeline.
    :param pipeline_url: string with a url to a pipeline
    :return: server_url, project_path
    """
    pattern = r"(https?://[^ /]+)/(.*)/-/pipelines/\d+"
    _match = re.match(pattern, pipeline_url)
    if not _match and len(_match.groups() != 2):
        raise AssertionError(f"url {pipeline_url} doesn't follow the pattern {pattern}")
    return _match.groups()


def get_gitlab_pipeline_from_url(gl: Gitlab, pipeline_url: str, server_url: str = None) -> tuple[ProjectPipeline, Project]:
    """
    Extract the project and pipeline object from the url string
    :param gl: Gitlab object
    :param pipeline_url: string with a url to a pipeline
    :param server_url: optional string with the server part
    :return: ProjectPipeline, Project objects
    """
    server_url = server_url if server_url else GITLAB_URL
    pattern = rf"^{re.escape(server_url)}/(.*)/-/pipelines/([0-9]+)$"
    match = re.match(pattern, pipeline_url)
    if not match:
        raise AssertionError(f"url {pipeline_url} doesn't follow the pattern {pattern}")
    namespace_with_project, pipeline_id = match.groups()
    cur_project = gl.projects.get(namespace_with_project)
    pipe = cur_project.pipelines.get(pipeline_id)
    return pipe, cur_project


def get_gitlab_project(glab: Gitlab, name: str) -> Project:
    """Finds a specified gitlab project for given user"""
    if "/" in name:
        project_path = name
    else:
        glab.auth()
        username = glab.user.username
        project_path = f"{username}/{name}"
    return glab.projects.get(project_path)


def get_token_from_default_dir() -> str:
    """
    Retrieves the GitLab token from the default directory.

    Returns:
        str: The path to the GitLab token file.

    Raises:
        FileNotFoundError: If the token file is not found.
    """
    token_file = TOKEN_DIR / "gitlab-token"
    try:
        return str(token_file.resolve())
    except FileNotFoundError as ex:
        print(
            f"Could not find {token_file}, please provide a token file as an argument"
        )
        raise ex


def validate_gitlab_token(token: str) -> bool:
    # Match against recognised token prefixes
    token_suffix = None
    for token_type, token_prefix in TOKEN_PREFIXES.items():
        if token.startswith(token_prefix):
            logging.info(f"Found probable token type: {token_type}")
            token_suffix = token[len(token_prefix):]
            break

    if not token_suffix:
        return False

    # Basic validation of the token suffix based on:
    # https://gitlab.com/gitlab-org/gitlab/-/blob/master/gems/gitlab-secret_detection/lib/gitleaks.toml
    if not re.match(r"(\w+-)?[0-9a-zA-Z_\-]{20,64}", token_suffix):
        return False

    return True


def get_token_from_arg(token_arg: str | Path | None) -> str | None:
    if not token_arg:
        logging.info("No token provided.")
        return None

    token_path = Path(token_arg)
    if token_path.is_file():
        return read_token_from_file(token_path)

    return handle_direct_token(token_path, token_arg)


def read_token_from_file(token_path: Path) -> str:
    token = token_path.read_text().strip()
    logging.info(f"Token read from file: {token_path}")
    return token


def handle_direct_token(token_path: Path, token_arg: str | Path) -> str | None:
    if token_path == Path(get_token_from_default_dir()):
        logging.warning(
            f"The default token file {token_path} was not found. "
            "Please provide a token file or a token directly via --token arg."
        )
        return None
    logging.info("Token provided directly as an argument.")
    return str(token_arg)


def read_token(token_arg: str | Path | None) -> str | None:
    token = get_token_from_arg(token_arg)
    if token and not validate_gitlab_token(token):
        logging.warning("The provided token is either an old token or does not seem to "
                        "be a valid token.")
        logging.warning("Newer tokens are the ones created from a Gitlab 14.5+ instance.")
        logging.warning("See https://about.gitlab.com/releases/2021/11/22/"
                        "gitlab-14-5-released/"
                        "#new-gitlab-access-token-prefix-and-detection")
    return token


def wait_for_pipeline(
    projects: set[Project],
    sha: str,
    timeout=None,
) -> tuple[Optional[ProjectPipeline],Optional[Project]]:
    """await until pipeline appears in Gitlab"""
    project_names = [project.path_with_namespace for project in projects]
    print(f"⏲ for the pipeline to appear in {project_names}..", end="")
    start_time = time.time()
    while True:
        for project in projects:
            pipelines = project.pipelines.list(sha=sha)
            if pipelines:
                print("", flush=True)
                return (pipelines[0], project)
        print("", end=".", flush=True)
        if timeout and time.time() - start_time > timeout:
            print(" not found", flush=True)
            return (None, None)
        time.sleep(1)

@cache
def is_gitlab_job() -> bool:
    return os.getenv("CI_JOB_ID") is not None
