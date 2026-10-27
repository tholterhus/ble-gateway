#!/bin/bash
# ============================================================
#  OTA-Update ohne USB-Kabel:
#    ./ota.sh              -> sucht das Gerät automatisch
#    ./ota.sh 192.168.1.50 -> explizite IP/Hostname
#
#  Das OTA-Passwort kommt aus platformio_local.ini
#  (--auth=..., muss zur LAUFENDEN Firmware passen).
# ============================================================
set -e
cd "$(dirname "$0")"

PIO="$HOME/.platformio/penv/bin/pio"
[ -x "$PIO" ] || PIO="$(command -v pio)"
if [ ! -x "$PIO" ]; then
    echo "PlatformIO nicht gefunden (pio). Erwartet: ~/.platformio/penv/bin/pio"
    exit 1
fi

TARGET="$1"
if [ -z "$TARGET" ]; then
    HOST=$(sed -n 's/^#define HOSTNAME *"\(.*\)"/\1/p' include/config.h | head -1)
    for CAND in "$HOST.local" "$HOST.at.home"; do
        [ -z "$CAND" ] && continue
        IP=$(ping -c1 -t2 "$CAND" 2>/dev/null | head -1 | grep -oE '[0-9]+(\.[0-9]+){3}')
        if [ -n "$IP" ]; then TARGET="$IP"; break; fi
    done
fi

if [ -z "$TARGET" ]; then
    echo "Gerät nicht gefunden — IP explizit angeben: ./ota.sh 192.168.x.x"
    echo "IP explizit angeben: ./ota.sh 192.168.178.251"
    exit 1
fi

echo "OTA-Ziel: $TARGET"
exec "$PIO" run -t upload --upload-port "$TARGET"
