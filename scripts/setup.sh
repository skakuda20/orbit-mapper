#!/usr/bin/env bash
set -euo pipefail

# Ubuntu setup: installs core Qt + build dependencies and builds with SGP4 disabled.
#
# Usage:
#   ./scripts/setup.sh
#
# Optional env vars:
#   QT_VERSION=6|5        (default: 5)
#   BUILD_DIR=build       (default: build)

QT_VERSION="${QT_VERSION:-5}"
BUILD_DIR="${BUILD_DIR:-build}"

repo_root() {
  cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
}

install_deps_ubuntu() {
  sudo apt-get update

  # Core build dependencies
  sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libgl1-mesa-dev

  # Qt dependencies
  if [[ "$QT_VERSION" == "6" ]]; then
    sudo apt-get install -y \
      qt6-base-dev \
      qt6-base-dev-tools \
      libqt6opengl6-dev
  else
    sudo apt-get install -y \
      qtbase5-dev \
      libqt5opengl5-dev
  fi
}

build_project() {
  local root
  root="$(repo_root)"

  cmake -S "$root" -B "$root/$BUILD_DIR" \
    -DORBIT_MAPPER_ENABLE_SGP4=OFF \
    -DORBIT_MAPPER_QT_VERSION="$QT_VERSION"

  cmake --build "$root/$BUILD_DIR" -j

  echo
  echo "Built: $root/$BUILD_DIR/orbit_mapper"
  echo "Run:   (cd $root/$BUILD_DIR && ./orbit_mapper)"
}

main() {
  echo "==> Installing dependencies (Ubuntu, QT_VERSION=$QT_VERSION)"
  install_deps_ubuntu

  echo "==> Configuring and building (SGP4 disabled)"
  build_project
}

main "$@"
