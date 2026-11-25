#!/usr/bin/env bash

set -euo pipefail

WORKSPACE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROS_DISTRO="${ROS_DISTRO:-humble}"
ROS_SETUP="/opt/ros/${ROS_DISTRO}/setup.bash"

if [[ ! -f "${ROS_SETUP}" ]]; then
  printf 'ROS 2 setup file not found: %s\n' "${ROS_SETUP}" >&2
  exit 1
fi

source "${ROS_SETUP}"
cd "${WORKSPACE_DIR}"

colcon build "$@"

printf '\nBuild complete. Load the workspace with:\n'
printf 'source %q\n' "${WORKSPACE_DIR}/install/setup.bash"
