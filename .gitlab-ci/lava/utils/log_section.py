import re
from dataclasses import dataclass
from datetime import timedelta
from enum import Enum, auto
from typing import Optional, Pattern, Union

from lava.utils.gitlab_section import GitlabSection


class LogSectionType(Enum):
    UNKNOWN = auto()
    LAVA_BOOT = auto()
    TEST_SUITE = auto()
    TEST_CASE = auto()
    LAVA_POST_PROCESSING = auto()


FALLBACK_GITLAB_SECTION_TIMEOUT = timedelta(minutes=10)
DEFAULT_GITLAB_SECTION_TIMEOUTS = {
    # Empirically, successful device boot in LAVA time takes less than 3
    # minutes.
    # LAVA itself is configured to attempt thrice to boot the device,
    # summing up to 9 minutes.
    # It is better to retry the boot than cancel the job and re-submit to avoid
    # the enqueue delay.
    LogSectionType.LAVA_BOOT: timedelta(minutes=9),
    # Test suite phase is where the initialization happens.
    LogSectionType.TEST_SUITE: timedelta(minutes=5),
    # Test cases may take a long time, this script has no right to interrupt
    # them. But if the test case takes almost 1h, it will never succeed due to
    # Gitlab job timeout.
    LogSectionType.TEST_CASE: timedelta(minutes=60),
    # LAVA post processing may refer to a test suite teardown, or the
    # adjustments to start the next test_case
    LogSectionType.LAVA_POST_PROCESSING: timedelta(minutes=5),
}


@dataclass(frozen=True)
class LogSection:
    regex: Union[Pattern, str]
    levels: tuple[str]
    section_id: str
    section_header: str
    section_type: LogSectionType
    collapsed: bool = False

    def from_log_line_to_section(
        self, lava_log_line: dict[str, str]
    ) -> Optional[GitlabSection]:
        if lava_log_line["lvl"] not in self.levels:
            return

        if match := re.search(self.regex, lava_log_line["msg"]):
            section_id = self.section_id.format(*match.groups())
            section_header = self.section_header.format(*match.groups())
            return GitlabSection(
                id=section_id,
                header=section_header,
                type=self.section_type,
                start_collapsed=self.collapsed,
            )


LOG_SECTIONS = (
    LogSection(
        regex=re.compile(r"<?STARTTC>? ([^>]*)"),
        levels=("target", "debug"),
        section_id="{}",
        section_header="test_case {}",
        section_type=LogSectionType.TEST_CASE,
    ),
    LogSection(
        regex=re.compile(r"<?STARTRUN>? ([^>]*)"),
        levels=("target", "debug"),
        section_id="{}",
        section_header="test_suite {}",
        section_type=LogSectionType.TEST_SUITE,
    ),
    LogSection(
        regex=re.compile(r"ENDTC>? ([^>]+)"),
        levels=("target", "debug"),
        section_id="post-{}",
        section_header="Post test_case {}",
        collapsed=True,
        section_type=LogSectionType.LAVA_POST_PROCESSING,
    ),
)
