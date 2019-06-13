#!/usr/bin/env python3

from jinja2 import Environment, FileSystemLoader
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--template")
parser.add_argument("--base-artifacts-url")
args = parser.parse_args()

env = Environment(loader = FileSystemLoader('.'), trim_blocks=True, lstrip_blocks=True)
template = env.get_template(args.template)

values = {}
values['base_artifacts_url'] = args.base_artifacts_url

print(template.render(values))