#!/usr/bin/env bash
set -Eeuo pipefail

script_path="${BASH_SOURCE[0]}"
script_dir="${script_path%/*}"
if [[ "$script_dir" == "$script_path" ]]; then
  script_dir="."
fi
repo_root="$(cd "$script_dir/.." && pwd -P)"
deps_dir="$repo_root/.deps"
emsdk_dir="$deps_dir/emsdk"
venv_dir="$repo_root/.venv"
emsdk_version="${EMSDK_VERSION:-6.0.0}"
skip_system_packages=0
skip_native_compiler=0
web_only=0
skip_python_venv=0
skip_npm_install=0
verify_build=0

usage() {
  printf '%s\n' \
'Usage: scripts/install-ubuntu.sh [options]' \
'' \
'Installs Rocket Rogue development dependencies for Ubuntu or WSL Ubuntu.' \
'' \
'Options:' \
'  --emsdk-version VERSION     Emscripten SDK version to install. Defaults to EMSDK_VERSION or 6.0.0.' \
'  --skip-system-packages      Do not install apt packages.' \
'  --skip-native-compiler      Do not install build-essential.' \
'  --web-only                  Skip all native compiler, SDL, window-system, and smoke-test packages.' \
'  --skip-python-venv          Do not create/update .venv.' \
'  --skip-npm-install          Do not run npm install.' \
'  --verify-build              Configure/build/test native and web targets after installing.' \
'  -h, --help                  Show this help.' \
'' \
'Recommended:' \
'  Keep this repo in the Linux filesystem, for example ~/src/RocketGame, not /mnt/c or OneDrive.'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --emsdk-version)
      emsdk_version="${2:?Missing value for --emsdk-version}"
      shift 2
      ;;
    --skip-system-packages)
      skip_system_packages=1
      shift
      ;;
    --skip-native-compiler)
      skip_native_compiler=1
      shift
      ;;
    --web-only)
      web_only=1
      skip_native_compiler=1
      shift
      ;;
    --skip-python-venv)
      skip_python_venv=1
      shift
      ;;
    --skip-npm-install)
      skip_npm_install=1
      shift
      ;;
    --verify-build)
      verify_build=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

run() {
  echo "+ $*"
  "$@"
}

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command after installation: $1" >&2
    exit 1
  fi
}

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "This script is intended for Ubuntu or WSL Ubuntu. Use scripts/install-windows.ps1 on Windows." >&2
  exit 1
fi

if grep -qi microsoft /proc/version 2>/dev/null; then
  echo "Detected WSL. For best performance, build from the Linux filesystem, not /mnt/c or OneDrive."
fi

cd "$repo_root"
mkdir -p "$deps_dir"
export DEBIAN_FRONTEND="${DEBIAN_FRONTEND:-noninteractive}"

if [[ "$skip_system_packages" -eq 0 ]]; then
  packages=(
    ca-certificates git cmake ninja-build python3 python3-venv nodejs npm
  )
  if [[ "$web_only" -eq 0 ]]; then
    packages+=(
      pkg-config binutils mesa-vulkan-drivers vulkan-tools vulkan-validationlayers
      glslc spirv-tools libx11-dev libxext-dev libxrandr-dev
      libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libxkbcommon-dev
      libwayland-dev wayland-protocols libudev-dev libdbus-1-dev libdecor-0-dev
      xvfb xauth mesa-utils
    )
    if [[ "$skip_native_compiler" -eq 0 ]]; then
      packages+=(build-essential)
    fi
  fi

  run sudo apt-get update
  run sudo apt-get install -y "${packages[@]}"
fi

require_command git
require_command cmake
require_command ninja
require_command python3
require_command node
require_command npm

if [[ "$skip_python_venv" -eq 0 ]]; then
  if [[ ! -d "$venv_dir" ]]; then
    run python3 -m venv "$venv_dir"
  fi
  # shellcheck disable=SC1091
  source "$venv_dir/bin/activate"
  run python -m pip install --upgrade pip
  if grep -Ev '^\s*(#|$)' "$repo_root/requirements-dev.txt" >/dev/null; then
    run python -m pip install -r "$repo_root/requirements-dev.txt"
  fi
fi

if [[ ! -d "$emsdk_dir/.git" ]]; then
  run git clone https://github.com/emscripten-core/emsdk.git "$emsdk_dir"
else
  run git -C "$emsdk_dir" fetch --tags --prune
  run git -C "$emsdk_dir" pull --ff-only
fi

run "$emsdk_dir/emsdk" install "$emsdk_version"
run "$emsdk_dir/emsdk" activate "$emsdk_version"
# shellcheck disable=SC1091
source "$emsdk_dir/emsdk_env.sh"

if [[ "$skip_npm_install" -eq 0 ]]; then
  if [[ -f "$repo_root/package-lock.json" ]]; then
    run npm ci
  else
    run npm install
  fi
fi

require_command emcc
run node tools/sanity-check.mjs

if [[ "$verify_build" -eq 1 && "$web_only" -eq 0 ]]; then
  run cmake --preset native-debug
  run cmake --build --preset native-debug
  run ctest --preset native-debug
  run cmake --preset native-release
  run cmake --build --preset native-release
  run ctest --preset native-release
  run cmake --build --preset package-native
  run cmake --preset web-release
  run cmake --build --preset web-release
  run ctest --preset web-release
elif [[ "$verify_build" -eq 1 ]]; then
  run cmake --preset web-release
  run cmake --build --preset web-release
  run ctest --preset web-release
fi

if [[ "$web_only" -eq 1 ]]; then
  cat <<EOF

Rocket Rogue web dependencies are installed.

For each new shell:
  source scripts/env-ubuntu.sh

Build and run the browser version:
  cmake --preset web-release
  cmake --build --preset web-release
  npm run serve:web

Installed Emscripten SDK:
  $emsdk_dir
EOF
else
  cat <<EOF

Rocket Rogue dependencies are installed.

For each new shell:
  source scripts/env-ubuntu.sh

Build and run the native game:
  cmake --preset native-debug
  cmake --build --preset native-debug
  build/native-debug/bin/RocketRogue

Build and run the browser version:
  cmake --preset web-release
  cmake --build --preset web-release
  npm run serve:web

Installed Emscripten SDK:
  $emsdk_dir
EOF
fi
