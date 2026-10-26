# PlatformIO-espressif32-Workaround:
# Der Builder hängt beim espota-Protokoll die eigenen upload_flags
# NICHT an (UPLOADERFLAGS wird hartkodiert gesetzt) -> OTA-Auth schlägt
# immer fehl. Dieses Post-Script liest das Passwort aus der lokalen
# platformio_local.ini (gitignored) und hängt es selbstständig an.
#
# Format in platformio_local.ini:
#     upload_flags = --auth=DEIN_PASSWORT
Import("env")
import os
import re

pw = ""
try:
    with open(os.path.join(env.subst("$PROJECT_DIR"), "platformio_local.ini")) as f:
        m = re.search(r"--auth=([^\s\n]+)", f.read())
        if m:
            pw = m.group(1)
except OSError:
    pass

if pw and "espota.py" in env.subst("$UPLOADER"):
    env.Append(UPLOADERFLAGS=["--auth", pw])
