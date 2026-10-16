#!/usr/bin/env bash
# Appaire automatiquement le robot BatMobile (options transmises : --repair, --timeout N).
set -euo pipefail

# Dossier du script, pour pouvoir le lancer depuis n'importe où
cd "$(dirname "$(readlink -f "$0")")"

[ -x build/bt_pairing ] || ./build.sh
exec build/bt_pairing "$@"
