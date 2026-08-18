#!/usr/bin/env bash

set -euxo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
plugins_dir="${repo_root}/unity/UnityGoDiceTest/Assets/Plugins"
dll_source="${repo_root}/windows/GoDiceDll.dll"
dll_destination="${plugins_dir}/GoDiceDll.dll"

[[ -n "${repo_root}" && -d "${repo_root}/.git" && -f "${dll_source}" ]]
/bin/mkdir -p "${plugins_dir}"
/bin/cp "${dll_source}" "${dll_destination}"
