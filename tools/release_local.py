#!/usr/bin/env python3
"""Fertige Firmware nach release/ legen (wie bei Simple Open Camper) und den Online-Flasher darauf zeigen lassen.

  python tools/release_local.py         # aus .pio/build/cyd (vorher bauen und den Quelltext committen)
  python tools/release_local.py --dry   # nur anzeigen

Ergebnis:
  release/huawei-soyo-cyd.bin      Komplettdatei (Bootloader, Partitionen, boot_app0, Firmware) – per USB ab 0x0,
                                   loescht die Einstellungen
  release/ota/huawei-soyo-cyd.bin  nur die Firmware – per USB an 0x10000, Einstellungen bleiben
  release/versionen.txt, release/README.md
Die Dateien bekommen "Version (Commit)" des aktuellen HEAD – deshalb erst committen, dann dieses Skript, dann die
release-Dateien als eigenen Commit.
"""
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import make_flasher as mf  # noqa: E402

DRY = '--dry' in sys.argv


def run(*a):
    return subprocess.run(a, cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def main():
    ver = mf.version()
    sha = run('git', 'rev-parse', '--short=7', 'HEAD')
    if run('git', 'status', '--porcelain', '--', 'src', 'include', 'platformio.ini', 'assets'):
        print('WARNUNG: Quelltext hat nicht committete Aenderungen – Version/Commit passt dann nicht genau.')
    app = os.path.join(mf.BUILD, 'firmware.bin')
    parts = [('0x1000', os.path.join(mf.BUILD, 'bootloader.bin')), ('0x8000', os.path.join(mf.BUILD, 'partitions.bin')),
             ('0xE000', mf.BOOT_APP0), ('0x10000', app)]
    missing = [f for _, f in parts if not os.path.exists(f)]
    if missing:
        print('Fehlt (erst bauen):', ', '.join(missing))
        sys.exit(1)
    print(f'Version {ver} ({sha}), Firmware vom {datetime.fromtimestamp(os.path.getmtime(app)):%Y-%m-%d %H:%M}')
    if DRY:
        return
    os.makedirs(os.path.join(mf.REL, 'ota'), exist_ok=True)
    full = os.path.join(mf.REL, mf.NAME + '.bin')
    shutil.copyfile(app, os.path.join(mf.REL, 'ota', mf.NAME + '.bin'))
    if not mf.merge(full, parts):
        sys.exit(1)
    with open(os.path.join(mf.REL, 'versionen.txt'), 'w', encoding='utf-8', newline='\n') as fh:
        fh.write(f'{mf.NAME}.bin {ver} ({sha})\n')

    def kb(p):
        return f'{os.path.getsize(p) // 1024} KB'
    L = [f'# Fertige Firmware – Version {ver}', '',
         f'Gebaut aus Commit {sha} ({datetime.now(timezone.utc):%Y-%m-%d %H:%M} UTC).', '',
         '**Am einfachsten:** [Online-Flasher](https://cyclemat.github.io/huawei-soyo-cyd/flasher/) im Browser',
         '(Chrome oder Edge am PC), Geraet per USB anschliessen, „Nur Firmware (Update)“ oder „Komplett (neu einrichten)“.', '',
         '**Per USB (komplett, Adresse 0x0):** `esptool.py --chip esp32 write_flash 0x0 huawei-soyo-cyd.bin` – fuer neue',
         'Geraete. **Achtung: loescht die Einstellungen** (WLAN, Passwoerter, Geraete), weil die Datei auch den',
         'Einstellungsbereich enthaelt.', '',
         '**Per USB, Einstellungen behalten:** `esptool.py --chip esp32 write_flash 0x10000 ota/huawei-soyo-cyd.bin`.', '',
         '| Geraet | Version | Komplett (USB, 0x0) | Nur Firmware (USB, 0x10000) |', '|---|---|---|---|',
         f'| ESP32-2432S028R (CYD) | {ver} ({sha}) | [huawei-soyo-cyd.bin](huawei-soyo-cyd.bin) ({kb(full)}) '
         f'| [ota/huawei-soyo-cyd.bin](ota/huawei-soyo-cyd.bin) ({kb(app)}) |', '',
         'Die Firmware hat kein Update ueber die Weboberflaeche (Partition `huge_app`, kein OTA).']
    with open(os.path.join(mf.REL, 'README.md'), 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(L) + '\n')
    print('release/ aktualisiert:', kb(full), 'komplett,', kb(app), 'Firmware')
    mf.main()


if __name__ == '__main__':
    main()
