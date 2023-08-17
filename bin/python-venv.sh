#!/usr/bin/env bash
set -eu

readonly requirements_file=$1
shift

venv_dir="$(dirname "$requirements_file")"/.venv
readonly venv_dir
readonly venv_req=$venv_dir/requirements.txt

if ! [ -r "$venv_dir/bin/activate" ]
then
  echo "Creating Python environment..."
  python -m venv "$venv_dir"
fi

# shellcheck disable=1091
source "$venv_dir/bin/activate"

if ! cmp --quiet "$requirements_file" "$venv_req"
then
  echo "$(realpath --relative-to="$PWD" "$requirements_file") has changed, re-installing..."
  pip --disable-pip-version-check install --requirement "$requirements_file"
  cp "$requirements_file" "$venv_req"
fi

python "$@"
