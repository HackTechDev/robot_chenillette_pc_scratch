#!/usr/bin/env bash
# Lance BatMobile Scratch, la programmation par blocs du robot (compile au besoin).
set -euo pipefail

# Dossier du script, pour pouvoir le lancer depuis n'importe où
cd "$(dirname "$(readlink -f "$0")")"

[ -x build/batmobile_scratch ] || ./build.sh
exec build/batmobile_scratch "$@"
