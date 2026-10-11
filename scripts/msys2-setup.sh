#!/usr/bin/env bash
# Windows / MSYS2 (UCRT64 shell) one-time setup + build. Run from the project root:
#   bash scripts/msys2-setup.sh            # set up Python tools, extract assets, build
#   bash scripts/msys2-setup.sh --no-build # only set up the Python tools
# Needs earthbound.sfc (US retail ROM, raw .sfc) in the project root. Nothing is downloaded except
# MSYS2 packages and pure-Python wheels; pydantic/Pillow/PyYAML come from pacman because pip has
# no Windows-MSYS wheels for them (pydantic-core would need a Rust toolchain).
set -euo pipefail
cd "$(dirname "$0")/.."

[ "${MSYSTEM:-}" = "UCRT64" ] || { echo "Open the 'MSYS2 UCRT64' shell, not '$MSYSTEM'." >&2; exit 1; }

pacman -S --needed --noconfirm \
    mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-vulkan-headers mingw-w64-ucrt-x86_64-vulkan-loader \
    mingw-w64-ucrt-x86_64-shaderc mingw-w64-ucrt-x86_64-glslang \
    mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-pip \
    mingw-w64-ucrt-x86_64-python-yaml mingw-w64-ucrt-x86_64-python-pillow mingw-w64-ucrt-x86_64-python-pydantic mingw-w64-ucrt-x86_64-python-pydantic-core

# pacman can believe a package is installed while its files are gone (damaged package database).
# Detect that and force a reinstall of just those packages.
if ! python -c "import yaml, PIL, pydantic, encodings" 2>/dev/null; then
    echo "Python modules missing on disk although pacman lists them; reinstalling..."
    pacman -S --noconfirm --overwrite '/ucrt64/*' \
        mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-pip \
        mingw-w64-ucrt-x86_64-python-yaml mingw-w64-ucrt-x86_64-python-pillow mingw-w64-ucrt-x86_64-python-pydantic mingw-w64-ucrt-x86_64-python-pydantic-core
fi
python -c "import yaml, PIL, pydantic; print('python modules ok')"

rm -rf .venv
python -m venv --system-site-packages .venv
# shellcheck disable=SC1091
source .venv/bin/activate
export SETUPTOOLS_SCM_PRETEND_VERSION="${SETUPTOOLS_SCM_PRETEND_VERSION:-0.1.0}"
pip install cyclopts
pip install -e . --no-deps
command -v ebtools >/dev/null || { echo "ebtools was not installed" >&2; exit 1; }

[ -f earthbound.sfc ] || { echo "Put your US retail ROM here as earthbound.sfc, then run this script again." >&2; exit 1; }
ebtools extract
[ "${1:-}" = "--no-build" ] && exit 0

cd port/unix
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEBTOOLS_EXE="$(cygpath -m "$(command -v ebtools)")"
cmake --build build
echo "Built: $(pwd)/build/earthbound.exe  (copy SDL2.dll from /ucrt64/bin next to it to run from Explorer)"
