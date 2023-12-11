#/usr/bin/env bash

set -euxo pipefail

/bin/echo "build windows dll"
bazel build //windows:dll_zip
/usr/bin/unzip -o bazel-bin/windows/dll_zip.zip -d unity/UnityGoDiceTest/Assets/Plugins/
