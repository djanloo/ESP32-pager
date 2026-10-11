#!/usr/bin/env bash
# Compila e flasha un cercapersone con il nome indicato.
#
# Uso: ./flash.sh <s3|esp32> <porta> <nome>
#   es. ./flash.sh esp32 /dev/ttyUSB0 marco
#
# Le impostazioni comuni (Wi-Fi, URL del server) vengono da sdkconfig.local
# e sono ricopiate a ogni flash; il nome è quello passato sulla riga di comando.
set -euo pipefail
cd "$(dirname "$0")"

usage() {
    echo "uso: $0 <s3|esp32> <porta> <nome>" >&2
    echo "  es. $0 esp32 /dev/ttyUSB0 marco" >&2
    exit 2
}

[ $# -eq 3 ] || usage
board=$1
port=$2
name=$3

case "$board" in
    s3)    target=esp32s3; build_dir=build;       sdkconfig=sdkconfig ;;
    esp32) target=esp32;   build_dir=build-esp32; sdkconfig=sdkconfig.esp32 ;;
    *)     echo "tipo di scheda sconosciuto: '$board' (usa s3 o esp32)" >&2; usage ;;
esac

if ! [[ "$name" =~ ^[a-z0-9_-]{1,32}$ ]]; then
    echo "nome non valido: '$name' (ammessi a-z 0-9 _ -, max 32 caratteri)" >&2
    exit 2
fi
if [ ! -e "$port" ]; then
    echo "porta '$port' non trovata. Schede collegate:" >&2
    ls -l /dev/serial/by-id/ 2>/dev/null >&2 || echo "  nessuna" >&2
    exit 2
fi
if [ ! -f sdkconfig.local ]; then
    echo "manca sdkconfig.local: copia sdkconfig.local.example e completalo" >&2
    exit 2
fi

idf() { ./idf.sh -B "$build_dir" -D SDKCONFIG="$sdkconfig" "$@"; }

# Prima volta per questo tipo di scheda: crea la configurazione per il target giusto.
if [ ! -f "$sdkconfig" ] || ! grep -q "^CONFIG_IDF_TARGET=\"$target\"$" "$sdkconfig"; then
    echo "== configurazione per $target"
    idf set-target "$target"
fi

# Ricopia le impostazioni comuni da sdkconfig.local, poi imposta il nome.
set_option() {
    local key=$1 line=$2
    if grep -q "^$key=" "$sdkconfig"; then
        KEY="$key" LINE="$line" python3 - "$sdkconfig" <<'EOF'
import os, sys
path, key, line = sys.argv[1], os.environ["KEY"], os.environ["LINE"]
with open(path) as f:
    lines = f.read().splitlines()
lines = [line if l.startswith(key + "=") else l for l in lines]
with open(path, "w") as f:
    f.write("\n".join(lines) + "\n")
EOF
    else
        echo "$line" >> "$sdkconfig"
    fi
}
while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
        CONFIG_PAGER_DEVICE_NAME=*) ;;
        CONFIG_*=*) set_option "${line%%=*}" "$line" ;;
    esac
done < sdkconfig.local
set_option CONFIG_PAGER_DEVICE_NAME "CONFIG_PAGER_DEVICE_NAME=\"$name\""

echo "== flash di \"$name\" ($target) su $port"
idf -p "$port" flash
echo "== fatto: \"$name\" flashato su $port"
