#!/usr/bin/env bash
# Compile BatMobile Scratch (build/batmobile_scratch) et l'outil d'appairage (build/bt_pairing).
set -euo pipefail

# Dossier du script, pour pouvoir le lancer depuis n'importe où
cd "$(dirname "$(readlink -f "$0")")"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"
