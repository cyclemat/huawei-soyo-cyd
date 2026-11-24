#!/usr/bin/env python3
"""Erzeugt Geraeteliste und Manifeste fuer den Web-Flasher (flasher/index.html, ESP Web Tools) – wie bei Simple Open Camper.

Quellen:
  release  – fertige Firmware aus release/ (huawei-soyo-cyd.bin komplett, ota/huawei-soyo-cyd.bin nur Firmware,
             Stand in release/versionen.txt) – die liegt im Repo und damit auch im Online-Flasher (GitHub Pages)
  lokal    – hier gebaute Firmware aus .pio/build/cyd (nur im lokalen Flasher, flasher.bat)
Je Quelle zwei Arten:
  komplett – Bootloader 0x1000, Partitionen 0x8000, boot_app0 0xE000, Firmware 0x10000 (ab 0x0): neu einrichten,
             Einstellungen weg
  update   – boot_app0 0xE000 + Firmware 0x10000: Einstellungen bleiben
Ergebnis: flasher/gen/ (lokal, nicht im Repo) und flasher/online/ (fuer GitHub Pages, im Repo).
Aufruf: python tools/make_flasher.py   (macht tools/flasher.py und tools/release_local.py von selbst)
"""
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, 'flasher', 'gen')
ONLINE = os.path.join(ROOT, 'flasher', 'online')
REL = os.path.join(ROOT, 'release')
BUILD = os.path.join(ROOT, '.pio', 'build', 'cyd')
NAME = 'huawei-soyo-cyd'
DEVICE = {'id': 'cyd', 'name': 'Huawei · Soyo – Cheap Yellow Display',
          'desc': 'ESP32-2432S028R (2,8″ Touch) – Nulleinspeisung mit SoyoSource und Huawei R48xx', 'chip': 'ESP32'}
APP_OFFSET = 0x10000
OTADATA_OFFSET = 0xE000
BOOT_APP0 = os.path.join(os.path.expanduser('~'), '.platformio', 'packages', 'framework-arduinoespressif32', 'tools',
                         'partitions', 'boot_app0.bin')
ESPTOOL = os.path.join(os.path.expanduser('~'), '.platformio', 'packages', 'tool-esptoolpy', 'esptool.py')
_PIO_PY = os.path.join(os.path.expanduser('~'), '.platformio', 'penv', 'Scripts', 'python.exe')
PY = _PIO_PY if os.path.exists(_PIO_PY) else sys.executable


def version():
    m = re.search(r'FW_VERSION\s+"([^"]+)"', open(os.path.join(ROOT, 'src', 'app.h'), encoding='utf-8').read())
    return m.group(1) if m else '?'


def merge(out, parts):
    """Komplettdatei ab 0x0 mit esptool zusammensetzen (merge_bin, neuere esptool: merge-bin)."""
    for cmd in ('merge_bin', 'merge-bin'):
        args = [PY, ESPTOOL, '--chip', 'esp32', cmd, '-o', out]
        for off, f in parts:
            args += [off, f]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode == 0:
            return True
    print('WARNUNG: esptool konnte die Komplettdatei nicht bauen:', r.stderr.strip()[-300:])
    return False


def manifest(name, ver, parts, erase):
    return {'name': name, 'version': ver, 'new_install_prompt_erase': erase, 'new_install_improv_wait_time': 0,
            'builds': [{'chipFamily': DEVICE['chip'], 'parts': [{'path': p, 'offset': o} for p, o in parts]}]}


def release_version():
    p = os.path.join(REL, 'versionen.txt')
    if os.path.exists(p):
        for line in open(p, encoding='utf-8'):
            k, _, v = line.strip().partition(' ')
            if k == NAME + '.bin':
                return v
    return '?'


def build(outdir, with_local):
    """outdir = flasher/gen (mit lokalem Build) oder flasher/online (nur Release)."""
    if os.path.isdir(outdir):
        shutil.rmtree(outdir)
    os.makedirs(os.path.join(outdir, 'm'))
    have_boot = os.path.exists(BOOT_APP0)
    if have_boot:
        shutil.copyfile(BOOT_APP0, os.path.join(outdir, 'boot_app0.bin'))
    sub = os.path.basename(outdir)
    up = '../../../'   # von flasher/<sub>/m/ zum Projektordner
    boot_part = [(up + 'flasher/%s/boot_app0.bin' % sub, OTADATA_OFFSET)] if have_boot else []
    dev = dict(DEVICE, src=[])
    full, ota = os.path.join(REL, NAME + '.bin'), os.path.join(REL, 'ota', NAME + '.bin')
    if os.path.exists(full):
        v = release_version()
        s = {'key': 'release', 'label': 'Aktuelle Version', 'version': v}
        mk = 'm/cyd-release-komplett.json'
        json.dump(manifest(DEVICE['name'], v, [(up + 'release/' + NAME + '.bin', 0)], True), open(os.path.join(outdir, mk), 'w'))
        s['komplett'] = mk
        if os.path.exists(ota):
            mk = 'm/cyd-release-update.json'
            json.dump(manifest(DEVICE['name'], v, boot_part + [(up + 'release/ota/' + NAME + '.bin', APP_OFFSET)], False),
                      open(os.path.join(outdir, mk), 'w'))
            s['update'] = mk
        dev['src'].append(s)
    app = os.path.join(BUILD, 'firmware.bin')
    if with_local and os.path.exists(app):
        v = version()
        when = datetime.fromtimestamp(os.path.getmtime(app)).strftime('%Y-%m-%d %H:%M')
        s = {'key': 'lokal', 'label': 'Lokal gebaut', 'version': v, 'when': when}
        shutil.copyfile(app, os.path.join(outdir, 'lokal-update.bin'))
        parts = [('0x1000', os.path.join(BUILD, 'bootloader.bin')), ('0x8000', os.path.join(BUILD, 'partitions.bin'))]
        if have_boot:
            parts.append(('0xE000', BOOT_APP0))
        parts.append(('0x10000', app))
        if all(os.path.exists(f) for _, f in parts) and merge(os.path.join(outdir, 'lokal-komplett.bin'), parts):
            mk = 'm/cyd-lokal-komplett.json'
            json.dump(manifest(DEVICE['name'], v, [(up + 'flasher/%s/lokal-komplett.bin' % sub, 0)], True),
                      open(os.path.join(outdir, mk), 'w'))
            s['komplett'] = mk
        mk = 'm/cyd-lokal-update.json'
        json.dump(manifest(DEVICE['name'], v, boot_part + [(up + 'flasher/%s/lokal-update.bin' % sub, APP_OFFSET)], False),
                  open(os.path.join(outdir, mk), 'w'))
        s['update'] = mk
        dev['src'].append(s)
    json.dump({'devices': [dev]}, open(os.path.join(outdir, 'geraete.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    return [s['label'] + ' ' + s['version'] for s in dev['src']]


def main():
    print('Flasher lokal (flasher/gen/):', ', '.join(build(GEN, True)) or 'keine Firmware')
    print('Online-Flasher (flasher/online/):', ', '.join(build(ONLINE, False)) or 'keine Firmware – erst tools/release_local.py')


if __name__ == '__main__':
    main()
