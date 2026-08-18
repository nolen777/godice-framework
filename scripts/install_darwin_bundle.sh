#!/usr/bin/env bash

set -euxo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${repo_root}/build/xcode-darwin-bundle"
plugins_dir="${repo_root}/unity/UnityGoDiceTest/Assets/Plugins"
bundle_source="${build_dir}/Build/Products/Release/DarwinGodiceBundle.bundle"
bundle_destination="${plugins_dir}/DarwinGodiceBundle.bundle"

[[ -n "${repo_root}" && -d "${repo_root}/.git" ]]
/bin/mkdir -p "${plugins_dir}"

/usr/bin/xcodebuild \
  -quiet \
  -project "${repo_root}/godice_client.xcodeproj" \
  -scheme DarwinGodiceBundle \
  -configuration Release \
  -derivedDataPath "${build_dir}" \
  CODE_SIGNING_ALLOWED=NO \
  build

[[ -d "${bundle_source}" ]]
/usr/bin/ditto "${bundle_source}" "${bundle_destination}"
