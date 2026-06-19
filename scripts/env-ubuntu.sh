#!/usr/bin/env bash

rocket_env_sourced=0
if [[ "${BASH_SOURCE[0]}" != "$0" ]]; then
  rocket_env_sourced=1
fi

rocket_script_path="${BASH_SOURCE[0]}"
rocket_script_dir="${rocket_script_path%/*}"
if [[ "$rocket_script_dir" == "$rocket_script_path" ]]; then
  rocket_script_dir="."
fi
rocket_repo_root="$(cd "$rocket_script_dir/.." && pwd -P)"
rocket_venv_activate="$rocket_repo_root/.venv/bin/activate"
rocket_emsdk_env="$rocket_repo_root/.deps/emsdk/emsdk_env.sh"

if [[ -f "$rocket_venv_activate" ]]; then
  # shellcheck disable=SC1090
  source "$rocket_venv_activate"
  echo "Activated Python virtual environment: $rocket_venv_activate"
else
  echo "Python virtual environment not found. Run scripts/install-ubuntu.sh first." >&2
fi

if [[ -f "$rocket_emsdk_env" ]]; then
  # shellcheck disable=SC1090
  source "$rocket_emsdk_env"
  echo "Activated Emscripten SDK environment: $rocket_emsdk_env"
else
  echo "Emscripten SDK environment not found. Run scripts/install-ubuntu.sh first." >&2
fi

echo
echo "Rocket Rogue dev shell is ready for this Bash session."
echo "Try: cmake --preset web-release && cmake --build --preset web-release && npm run serve:web"

if [[ "$rocket_env_sourced" -eq 0 ]]; then
  echo
  echo "Note: run this with 'source scripts/env-ubuntu.sh' to keep the environment active in your current shell."
fi
