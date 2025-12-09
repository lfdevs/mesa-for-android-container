#!/usr/bin/env python3
# Copyright © 2025 Collabora Ltd.
# Authors:
#   Sergi Blanch Torne <sergi.blanch.torne@collabora.com>
#
# SPDX-License-Identifier: MIT

from dataclasses import dataclass
from datetime import timedelta

from unittest import TestCase
from typing import Iterable

from marge_queue import MargeMergeRequest, MargeQueue, get_merge_queue


DATA = {
    37564: {
        # sample of a Merge Request rejected by marge and later reassigned to marge
        "updated_at": "2025-09-25T10:59:27.144Z",
        "title": "etnaviv: Fix util_blitter_save_so_targets(..) call",
        "notes": [
            # Those notes must be listed in reverse order
            # because the list is requested sort="desc"
            {
                "body": "assigned to @marge-bot and unassigned @austriancoder",
                "created_at": "2025-09-25T11:44:01.672Z",
            },
            {
                "body": "added 1 commit",
                "created_at": "2025-09-25T10:43:43.490Z",
            },
            {
                "body": "This branch couldn't be merged:",
                "created_at": "2025-09-25T10:42:27.817Z",
            },
            {
                "body": "assigned to @austriancoder and unassigned @marge-bot",
                "created_at": "2025-09-25T10:42:15.404Z",
            },
            {
                "body": "added 3 commits",
                "created_at": "2025-09-25T10:38:57.332Z",
            },
            {
                "body": "assigned to @marge-bot",
                "created_at": "2025-09-25T10:38:03.993Z",
            },
        ],
    },
    37560: {
        # sample of a Merge Request assigned to marge between the two
        # assignments of the previous sample. This is meant to show that
        # this goes first in the queue, and it doesn't confuses the sequence.
        "updated_at": "2025-09-25T11:38:08.420Z",
        "title": "tu: limit query pool types logged into RMV",
        "notes": [
            {
                "body": "assigned to @marge-bot",
                "created_at": "2025-09-25T10:47:40.040Z",
            },
            {
                "body": "added 1 commit",
                "created_at": "2025-09-25T10:46:42.113Z",
            },
            {
                "body": "mentioned in issue #13970",
                "created_at": "2025-09-25T10:45:35.566Z",
            },
            {
                "body": "Rb",
                "created_at": "2025-09-25T08:25:51.527Z",
            },
        ],
    },
}


@dataclass
class ProjectMergeRequestNote:
    body: str
    created_at: str


@dataclass
class ProjectMergeRequestNoteManager:
    """
    Mock the gitlab.v4.objects.ProjectMergeRequestNoteManager
    """
    iid: int

    def list(self, *args, **kwargs) -> Iterable:
        for note in DATA[self.iid]["notes"]:
            yield ProjectMergeRequestNote(**note)


@dataclass
class ProjectMergeRequest:
    """
    Mock the gitlab.v4.objects.ProjectMergeRequest
    """
    iid: int

    @property
    def updated_at(self) -> str:
        return DATA[self.iid]["updated_at"]

    @property
    def web_url(self) -> str:
        return f"https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/{self.iid}"

    @property
    def title(self) -> str:
        return DATA[self.iid]["title"]

    @property
    def notes(self) -> ProjectMergeRequestNoteManager:
        return ProjectMergeRequestNoteManager(self.iid)


@dataclass
class ProjectMergeRequestManager:
    """
    Mock the gitlab.v4.objects.ProjectMergeRequestManager
    """
    def list(self, *args, **kwargs) -> Iterable:
        for mr in DATA.keys():
            yield ProjectMergeRequest(mr)


@dataclass
class Project:
    """
    Mock the gitlab.v4.objects.Project
    """
    @property
    def mergerequests(self) -> ProjectMergeRequestManager:
        return ProjectMergeRequestManager()


class MargeMergeTestCase(TestCase):
    def test_queue(self) -> None:
        queue = MargeQueue()
        for mr_id in DATA.keys():
            gl_mr_mock_obj = ProjectMergeRequest(mr_id)
            marge_mr = MargeMergeRequest(gl_mr_mock_obj)

            # for each element in the data sample, test the MargeMergeRequest construction
            self.assertEqual(marge_mr.title, gl_mr_mock_obj.title)
            self.assertEqual(marge_mr.web_url, gl_mr_mock_obj.web_url)
            self.assertIsInstance(marge_mr.time_enqueued, timedelta)

            queue.append(marge_mr)

        self.assertEqual(queue.n_merge_requests_enqueued, 2)


    def test_get_merge_queue(self) -> None:
        project = Project()
        queue = get_merge_queue(project)

        self.assertEqual(queue.n_merge_requests_enqueued, 2)
        self.assertEqual(len(queue.sorted_queue), 2)
        # They are in reverse order because of the reassignment to marge
        self.assertEqual(queue.sorted_queue[0].id, list(DATA.keys())[1])
        self.assertEqual(queue.sorted_queue[1].id, list(DATA.keys())[0])
        self.assertEqual(len(queue.undetermined), 0)

        for marge_mr in queue.sorted_queue:
            mr_id = marge_mr.id
            self.assertEqual(marge_mr.title, DATA[mr_id]["title"])
            self.assertEqual(
                marge_mr.web_url,
                f"https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/{mr_id}"
            )
            self.assertIsInstance(marge_mr.time_enqueued, timedelta)
