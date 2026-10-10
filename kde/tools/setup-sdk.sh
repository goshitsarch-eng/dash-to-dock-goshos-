#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
sdk_name=${GOSHOS_SDK_NAME:-goshos-dock-sdk}
sdk_image=archlinux@sha256:4e77cf2ea5f410e6f8be5abf93ccf17ce2436e87a138c167208356711a405dbd
if ! docker inspect "$sdk_name" >/dev/null 2>&1; then
    mounts=(--mount "type=bind,src=$repo_dir,dst=/src")
    if [[ -f /etc/ssl/certs/ca-certificates.crt ]]; then
        mounts+=(--mount type=bind,src=/etc/ssl/certs/ca-certificates.crt,dst=/run/host-ca.pem,readonly)
    fi
    docker run -d --init --name "$sdk_name" "${mounts[@]}" \
        -e HTTP_PROXY -e HTTPS_PROXY -e ALL_PROXY -e NO_PROXY \
        "$sdk_image" sleep infinity
fi
mounted_repo=$(docker inspect --format '{{range .Mounts}}{{if eq .Destination "/src"}}{{.Source}}{{end}}{{end}}' "$sdk_name")
if [[ "$mounted_repo" != "$repo_dir" ]]; then
    echo "Container $sdk_name belongs to another checkout; choose a different GOSHOS_SDK_NAME." >&2
    exit 1
fi
if [[ $(docker inspect --format '{{.State.Running}}' "$sdk_name") != true ]]; then
    docker start "$sdk_name" >/dev/null
fi
docker exec "$sdk_name" bash -c '
    set -euo pipefail
    if [[ -f /run/host-ca.pem ]] && ! grep -q "^XferCommand =" /etc/pacman.conf; then
        sed -i "/^\[options\]/a XferCommand = /usr/bin/curl --cacert /run/host-ca.pem -sS -L -C - --fail --output %o %u" /etc/pacman.conf
    fi
    /src/kde/tools/install-sdk-packages.sh
    cmake -S /src/kde -B /src/kde/build-sdk -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=ON
    cmake --build /src/kde/build-sdk --parallel 2
    ctest --test-dir /src/kde/build-sdk --output-on-failure --timeout 120
    cmake --install /src/kde/build-sdk
    chown -R kde-test:kde-test /src/kde/build-sdk
    runuser -u kde-test -- bash /src/kde/tools/test-wayland.sh /src/kde/build-sdk
'
printf 'SDK ready: %s\n' "$sdk_name"
