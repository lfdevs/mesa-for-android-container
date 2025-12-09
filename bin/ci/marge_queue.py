#!/usr/bin/env python3
# Copyright © 2020 - 2023 Collabora Ltd.
# Authors:
#   David Heidelberg <david.heidelberg@collabora.com>
#
# SPDX-License-Identifier: MIT

"""
Monitors Marge-bot and return number of assigned MRs.
"""

import argparse
import time
import sys
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from dateutil import parser
from traceback import print_exc
from typing import Optional

import gitlab
from gitlab.base import RESTObjectList
from gitlab.exceptions import GitlabAuthenticationError
from gitlab.v4.objects import Project, ProjectMergeRequest
from gitlab_common import get_token_from_default_dir, read_token, pretty_duration
from rich.console import Console

REFRESH_WAIT = 30
MARGE_BOT_USER_ID = 9716
ASSIGNED_TO_MARGE = "assigned to @marge-bot"
ASSIGNED_AT_PADDING = 19
WAITING_PADDING = 8
MR_ID_PADDING = 5

console = Console(highlight=False)
print = console.print


def parse_args() -> argparse.Namespace:
    """Parse args"""
    parse = argparse.ArgumentParser(
        description="Tool to show merge requests assigned to the marge-bot",
    )
    parse.add_argument(
        "--wait", action="store_true", help="wait until CI is free",
    )
    parse.add_argument(
        "--token",
        metavar="token",
        default=get_token_from_default_dir(),
        help="force GitLab token, otherwise it's read from ~/.config/gitlab-token",
    )
    return parse.parse_args()


@dataclass
class MargeMergeRequest:
    """Represent a Merge Request assigned to Marge"""
    id: int = field(init=False)
    mr: ProjectMergeRequest = field(repr=False)
    updated_at: datetime | None = field(init=False, repr=False)
    assigned_at: datetime | None = field(init=False, repr=False)

    def __post_init__(self):
        self.id = self.mr.iid
        self.updated_at = parser.parse(self.mr.updated_at)
        self.assigned_at = self.__find_last_assign_to_marge()

    def __find_last_assign_to_marge(self) -> Optional[datetime]:
        for note in self.__get_mr_notes_iterator():
            if note.body.startswith(ASSIGNED_TO_MARGE):
                return parser.parse(note.created_at)

    def __eq__(self, other: "MargeMergeRequest") -> bool:
        return self.id == other.id

    @property
    def time_enqueued(self) -> timedelta:
        if self.assigned_at is None:
            raise ValueError("Assign to marge timestamp not defined")
        return datetime.now(timezone.utc) - self.assigned_at

    @property
    def web_url(self) -> str:
        return self.mr.web_url

    @property
    def title(self) -> str:
        return self.mr.title

    def __get_mr_notes_iterator(self) -> RESTObjectList:
        return self.mr.notes.list(
            iterator=True, order_by="updated_at", sort="desc"
        )  # start with the most recent


@dataclass
class MargeQueue:
    """
    Collect and sort the merge requests assigned to marge
    """
    elements_sorted: dict[datetime, MargeMergeRequest] = field(
        init=False, repr=False, default_factory=dict
    )
    undetermined: list[MargeMergeRequest] = field(
        init=False, repr=False, default_factory=list
    )

    def append(self, mr: MargeMergeRequest) -> None:
        if mr.assigned_at is not None:
            self.elements_sorted[mr.assigned_at] = mr
        else:
            self.undetermined.append(mr)

    @property
    def n_merge_requests_enqueued(self) -> int:
        return len(self.elements_sorted) + len(self.undetermined)

    @property
    def sorted_queue(self) -> list[MargeMergeRequest]:
        """
        Provide a list of the elements that can be sorted based on the
        assignment to Marge.
        """
        return list(dict(sorted(self.elements_sorted.items())).values())

    @property
    def all_assigned(
        self
    ) -> list[MargeMergeRequest]:
        """
        Provide a single list, but in sorted, of all the elements found
        assigned to Marge. This include the elements in elements_sorted
        and the ones undetermined (expected to be empty).
        """
        return self.sorted_queue + self.undetermined


def get_merge_queue(project: Project) -> MargeQueue:
    queue = MargeQueue()
    for mr in __get_project_marge_merge_requests(project):
        marge_merge_request = MargeMergeRequest(mr)
        queue.append(marge_merge_request)
    for mr in queue.sorted_queue:
        assigned_at = f"{mr.assigned_at:%Y-%m-%d %H:%M:%S}"
        time_enqueued = f"{pretty_duration(mr.time_enqueued.total_seconds())}"
        print(
            f"⛭ {assigned_at:<{ASSIGNED_AT_PADDING}} "
            f"{time_enqueued:>{WAITING_PADDING}} "
            f"{link2print(mr.web_url, mr.id, MR_ID_PADDING)} {mr.title}"
        )
    if queue.undetermined:
        print(
            "Unable to determine when they where assigned to Marge:"
        )
        for mr in queue.undetermined:
            updated_at = f"{mr.updated_at:%Y-%m-%d %H:%M:%S}"
            print(
                f"⛭ {updated_at:<{ASSIGNED_AT_PADDING}} "
                f"{" ":{WAITING_PADDING}} "
                f"{link2print(mr.web_url, mr.id, MR_ID_PADDING)} {mr.title}"
            )
    return queue


def __get_gitlab_object(token: str) -> gitlab.Gitlab:
    return gitlab.Gitlab(url="https://gitlab.freedesktop.org", private_token=token, retry_transient_errors=True)


def __get_gitlab_project(gl: gitlab.Gitlab) -> Project:
    return gl.projects.get("mesa/mesa")


def __get_project_marge_merge_requests(
    project: Project
) -> list[ProjectMergeRequest]:
    return project.mergerequests.list(
        assignee_id=MARGE_BOT_USER_ID,
        scope="all",
        state="opened",
        get_all=True,
    )


def link2print(url: str, text: str, text_pad: int = 0) -> str:
    text = str(text)
    text_pad = len(text) if text_pad < 1 else text_pad
    return f"[link={url}]{text:{text_pad}}[/link]"


def main():
    args = parse_args()
    token = read_token(args.token)
    gl = __get_gitlab_object(token)

    project = __get_gitlab_project(gl)

    assigned_at = "Assigned at"
    waiting = "Waiting"
    mr = "MR"
    title = "Title"
    print(
        f"  {assigned_at:<{ASSIGNED_AT_PADDING}} "
        f"{waiting:<{WAITING_PADDING}} "
        f"{mr:<{MR_ID_PADDING}} {title}"
    )
    while True:
        try:
            n_mrs = get_merge_queue(project).n_merge_requests_enqueued
        except GitlabAuthenticationError as autherror:
            if autherror.response_code == 401:  # 401 Unauthorized
                print(f"Alert! Tool action requires authentication. Use the --token argument")
            else:
                print(f"Alert! Something went wrong: {autherror.responce_body}")
                print_exc()
            sys.exit(-1)

        print(f"Job waiting: {n_mrs}")

        if n_mrs == 0:
            sys.exit(0)
        if not args.wait:
            sys.exit(min(n_mrs, 127))

        try:
            time.sleep(REFRESH_WAIT)
        except KeyboardInterrupt:
            print(
                "Sleep interrupted from keyboard, "
                "doing one last iteration before finish."
            )
            args.wait = False


if __name__ == "__main__":
    main()
