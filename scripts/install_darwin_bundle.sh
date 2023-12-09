#/usr/bin/env bash

set -euxo pipefail

/bin/echo "build darwin bundle"
bazel build //darwin/framework:DarwinGodiceBundle
/usr/bin/unzip -o bazel-bin/darwin/framework/DarwinGodiceBundle.zip -d unity_test/UnityGoDiceTest/Assets/Plugins/