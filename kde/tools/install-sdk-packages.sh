#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run inside the disposable official Arch container, not on a user's desktop.
set -euo pipefail
if [[ ! -f /etc/arch-release || $EUID != 0 ]]; then
    echo "This helper requires root inside the Arch SDK container." >&2
    exit 1
fi
pacman -Syu --noconfirm --needed \
    base-devel cmake ninja extra-cmake-modules python nodejs \
    plasma-desktop plasma-workspace plasma-sdk plasma-pa kwin kirigami \
    qt6-5compat qt6-multimedia qt6-tools \
    libmalcontent libei wayland-protocols layer-shell-qt \
    mesa libglvnd vulkan-swrast vulkan-headers mesa-utils \
    xorg-xwayland xorg-server-xvfb xorg-xhost xdotool \
    dbus pipewire pipewire-pulse wireplumber dolphin konsole spectacle \
    gvfs gvfs-smb gvfs-nfs gvfs-dnssd \
    python-pyqt6 baloo-widgets
# Arch's optional realtime scheduler capability cannot be granted by ordinary
# Docker; removing it lets the test compositor use ordinary scheduling.
if [[ -n $(getcap /usr/bin/kwin_wayland) ]]; then
    setcap -r /usr/bin/kwin_wayland
fi
id -u kde-test >/dev/null 2>&1 || useradd --create-home --uid 1000 kde-test
fc-cache -f
