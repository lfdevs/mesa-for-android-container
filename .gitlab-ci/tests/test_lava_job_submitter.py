#!/usr/bin/env python3
#
# Copyright (C) 2022 Collabora Limited
# Author: Guilherme Gallo <guilherme.gallo@collabora.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import xmlrpc.client
from contextlib import nullcontext as does_not_raise
from datetime import datetime
from itertools import repeat
from typing import Tuple
from unittest.mock import MagicMock, patch

import pytest
import yaml
from freezegun import freeze_time
from lava.lava_job_submitter import (
    DEVICE_HANGING_TIMEOUT_SEC,
    follow_job_execution,
    hide_sensitive_data,
    retriable_follow_job,
)


def jobs_logs_response(finished=False, msg=None) -> Tuple[bool, str]:
    timed_msg = {"dt": str(datetime.now()), "msg": "New message"}
    logs = [timed_msg] if msg is None else msg

    return finished, yaml.safe_dump(logs)


def result_get_testjob_results_response() -> str:
    result = {"result": "test"}
    results = [{"metadata": result}]

    return yaml.safe_dump(results)


def result_get_testcase_results_response() -> str:
    result = {"result": "pass"}
    test_cases = [result]

    return yaml.safe_dump(test_cases)


@pytest.fixture
def mock_proxy():
    def create_proxy_mock(**kwargs):
        proxy_mock = MagicMock()
        proxy_submit_mock = proxy_mock.scheduler.jobs.submit
        proxy_submit_mock.return_value = "1234"

        proxy_results_mock = proxy_mock.results.get_testjob_results_yaml
        proxy_results_mock.return_value = result_get_testjob_results_response()

        proxy_test_cases_mock = proxy_mock.results.get_testcase_results_yaml
        proxy_test_cases_mock.return_value = result_get_testcase_results_response()

        proxy_logs_mock = proxy_mock.scheduler.jobs.logs
        proxy_logs_mock.return_value = jobs_logs_response()
        for key, value in kwargs.items():
            setattr(proxy_logs_mock, key, value)

        return proxy_mock

    yield create_proxy_mock


@pytest.fixture
def mock_proxy_waiting_time(mock_proxy):
    def update_mock_proxy(frozen_time, **kwargs):
        wait_time = kwargs.pop("wait_time", 0)
        proxy_mock = mock_proxy(**kwargs)
        proxy_job_state = proxy_mock.scheduler.job_state
        proxy_job_state.return_value = {"job_state": "Running"}
        proxy_job_state.side_effect = frozen_time.tick(wait_time)

        return proxy_mock

    return update_mock_proxy


@pytest.fixture
def mock_sleep():
    """Mock time.sleep to make test faster"""
    with patch("time.sleep", return_value=None):
        yield


@pytest.fixture
def frozen_time(mock_sleep):
    with freeze_time() as frozen_time:
        yield frozen_time


@pytest.mark.parametrize("exception", [RuntimeError, SystemError, KeyError])
def test_submit_and_follow_respects_exceptions(mock_sleep, mock_proxy, exception):
    with pytest.raises(exception):
        follow_job_execution(mock_proxy(side_effect=exception), "")


def generate_n_logs(n=1, tick_sec=1):
    """Simulate a log partitionated in n components"""
    with freeze_time(datetime.now()) as time_travel:
        while True:
            # Simulate a scenario where the target job is waiting for being started
            for _ in range(n - 1):
                time_travel.tick(tick_sec)
                yield jobs_logs_response(finished=False, msg=[])

            time_travel.tick(tick_sec)
            yield jobs_logs_response(finished=True)


NETWORK_EXCEPTION = xmlrpc.client.ProtocolError("", 0, "test", {})
XMLRPC_FAULT = xmlrpc.client.Fault(0, "test")

PROXY_SCENARIOS = {
    "finish case": (generate_n_logs(1), does_not_raise(), True),
    "works at last retry": (
        generate_n_logs(n=3, tick_sec=DEVICE_HANGING_TIMEOUT_SEC + 1),
        does_not_raise(),
        True,
    ),
    "timed out more times than retry attempts": (
        generate_n_logs(n=4, tick_sec=DEVICE_HANGING_TIMEOUT_SEC + 1),
        does_not_raise(),
        False,
    ),
    "long log case, no silence": (
        generate_n_logs(n=1000, tick_sec=0),
        does_not_raise(),
        True,
    ),
    "very long silence": (
        generate_n_logs(n=4, tick_sec=100000),
        does_not_raise(),
        False,
    ),
    # If a protocol error happens, _call_proxy will retry without affecting timeouts
    "unstable connection, ProtocolError followed by final message": (
        (NETWORK_EXCEPTION, jobs_logs_response(finished=True)),
        does_not_raise(),
        True,
    ),
    # After an arbitrary number of retries, _call_proxy should call sys.exit
    "unreachable case, subsequent ProtocolErrors": (
        repeat(NETWORK_EXCEPTION),
        pytest.raises(SystemExit),
        False,
    ),
    "XMLRPC Fault": ([XMLRPC_FAULT], pytest.raises(SystemExit, match="1"), False),
}


@patch("time.sleep", return_value=None)  # mock sleep to make test faster
@pytest.mark.parametrize(
    "side_effect, expectation, has_finished",
    PROXY_SCENARIOS.values(),
    ids=PROXY_SCENARIOS.keys(),
)
def test_retriable_follow_job(
    mock_sleep, side_effect, expectation, has_finished, mock_proxy
):
    with expectation:
        result = retriable_follow_job(mock_proxy(side_effect=side_effect), "")
        assert has_finished == result


WAIT_FOR_JOB_SCENARIOS = {
    "one log run taking (sec):": (generate_n_logs(1), True),
}


@pytest.mark.parametrize("wait_time", (0, DEVICE_HANGING_TIMEOUT_SEC * 2))
@pytest.mark.parametrize(
    "side_effect, has_finished",
    WAIT_FOR_JOB_SCENARIOS.values(),
    ids=WAIT_FOR_JOB_SCENARIOS.keys(),
)
def test_simulate_a_long_wait_to_start_a_job(
    frozen_time,
    wait_time,
    side_effect,
    has_finished,
    mock_proxy_waiting_time,
):
    start_time = datetime.now()
    result = retriable_follow_job(
        mock_proxy_waiting_time(
            frozen_time, side_effect=side_effect, wait_time=wait_time
        ),
        "",
    )

    end_time = datetime.now()
    delta_time = end_time - start_time

    assert has_finished == result
    assert delta_time.total_seconds() >= wait_time


SENSITIVE_DATA_SCENARIOS = {
    "no sensitive data tagged": (
        ["bla  bla", "mytoken: asdkfjsde1341=="],
        ["bla  bla", "mytoken: asdkfjsde1341=="],
        "HIDEME",
    ),
    "sensitive data tagged": (
        ["bla  bla", "mytoken: asdkfjsde1341== # HIDEME"],
        ["bla  bla"],
        "HIDEME",
    ),
    "sensitive data tagged with custom word": (
        ["bla  bla", "mytoken: asdkfjsde1341== # DELETETHISLINE", "third line"],
        ["bla  bla", "third line"],
        "DELETETHISLINE",
    ),
}


@pytest.mark.parametrize(
    "input, expectation, tag",
    SENSITIVE_DATA_SCENARIOS.values(),
    ids=SENSITIVE_DATA_SCENARIOS.keys(),
)
def test_hide_sensitive_data(input, expectation, tag):
    yaml_data = yaml.safe_dump(input)
    yaml_result = hide_sensitive_data(yaml_data, tag)
    result = yaml.safe_load(yaml_result)

    assert result == expectation
