#!/usr/bin/env python3

import argparse
import base64
import pathlib
import requests
import subprocess
import typing


def error(msg: str) -> None:
    print('\033[31m' + msg + '\033[0m')


class Source:
    def __init__(self, filename: str, url: typing.Optional[str],
                 template: typing.Optional[str] = None, remove:
                 typing.Optional[str] = None):
        self.file = pathlib.Path(filename)
        self.url = url
        self.template = template
        self.remove = remove

    def sync(self) -> None:
        if self.url is None:
            return

        print('Syncing {}...'.format(self.file), end=' ', flush=True)
        req = requests.get(self.url)

        if not req.ok:
            error('Failed to retrieve file: {} {}'.format(req.status_code, req.reason))
            return

        content = req.content

        content = str(content, encoding='utf-8')
        if self.remove is not None:
            content = content.replace(self.remove, '')
        if self.template is not None:
            content = self.template % content

        with open(self.file, 'w') as f:
            f.write(content)

        print('Done')

# rtinspector needs the docking branch of imgui
BASE_URL = 'https://raw.githubusercontent.com/ocornut/imgui/refs/heads/docking'

SOURCES = [
    Source('src/imgui/backends/imgui_impl_sdl3.cpp', f'{BASE_URL}/backends/imgui_impl_sdl3.cpp'),
    Source('src/imgui/backends/imgui_impl_sdl3.h', f'{BASE_URL}/backends/imgui_impl_sdl3.h'),
    Source('src/imgui/backends/imgui_impl_vulkan.cpp', f'{BASE_URL}/backends/imgui_impl_vulkan.cpp'),
    Source('src/imgui/backends/imgui_impl_vulkan.h', f'{BASE_URL}/backends/imgui_impl_vulkan.h'),
    Source('src/imgui/imconfig.h', f'{BASE_URL}/imconfig.h'),
    Source('src/imgui/imgui.cpp', f'{BASE_URL}/imgui.cpp'),
    Source('src/imgui/imgui.h', f'{BASE_URL}/imgui.h'),
    Source('src/imgui/imgui_draw.cpp', f'{BASE_URL}/imgui_draw.cpp'),
    Source('src/imgui/imgui_internal.h', f'{BASE_URL}/imgui_internal.h'),
    Source('src/imgui/imgui_tables.cpp', f'{BASE_URL}/imgui_tables.cpp'),
    Source('src/imgui/imgui_widgets.cpp', f'{BASE_URL}/imgui_widgets.cpp'),
    Source('src/imgui/imstb_rectpack.h', f'{BASE_URL}/imstb_rectpack.h'),
    Source('src/imgui/imstb_textedit.h', f'{BASE_URL}/imstb_textedit.h'),
    Source('src/imgui/imstb_truetype.h', f'{BASE_URL}/imstb_truetype.h'),
]


if __name__ == '__main__':
    git_toplevel = subprocess.check_output(['git', 'rev-parse', '--show-toplevel'],
                                           stderr=subprocess.DEVNULL).decode("ascii").strip()
    if not pathlib.Path(git_toplevel).resolve() == pathlib.Path('.').resolve():
        error('Please run this script from the root folder ({})'.format(git_toplevel))
        exit(1)

    for source in SOURCES:
        source.sync()
