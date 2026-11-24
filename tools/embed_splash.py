# Startbild beim Bauen einbetten -> src/generated/splash_jpg.h (laeuft als extra_scripts "pre:" in platformio.ini)
# Eigene splash_cyd.jpg im Projektordner hat Vorrang, sonst assets/splash_cyd.jpg (erzeugt mit tools/make_splash.py).
# Das Bild muss 320x240 gross und als JPEG "baseline" gespeichert sein; LovyanGFX zeichnet es direkt aus dem Flash.
# Ohne Bild: SPLASH_JPG_LEN 0, dann gibt es kein Startbild.
import os

try:
    Import("env")  # noqa: F821 (PlatformIO)
    root = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:
    root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

src = os.path.join(root, "splash_cyd.jpg")
if not os.path.exists(src):
    src = os.path.join(root, "assets", "splash_cyd.jpg")
dst = os.path.join(root, "src", "generated", "splash_jpg.h")
os.makedirs(os.path.dirname(dst), exist_ok=True)

jpg = b""
if os.path.exists(src):
    with open(src, "rb") as f:
        jpg = f.read()
    if jpg[:2] != b"\xff\xd8":
        print("WARNUNG: %s ist kein JPEG - Startbild weggelassen" % src)
        jpg = b""
    elif b"\xff\xc2" in jpg[:4096]:
        print("WARNUNG: %s ist 'progressive' gespeichert - bitte als 'baseline' speichern." % src)
        jpg = b""

out = ["// Automatisch erzeugt von tools/embed_splash.py - nicht bearbeiten.",
       "#pragma once", "#include <stdint.h>", "#include <stddef.h>", ""]
if jpg:
    out.append("static const uint8_t SPLASH_JPG[] = {")
    for i in range(0, len(jpg), 24):
        out.append("  " + ",".join(str(b) for b in jpg[i:i + 24]) + ",")
    out.append("};")
    out.append("static const size_t SPLASH_JPG_LEN = %d;" % len(jpg))
else:
    out.append("static const uint8_t SPLASH_JPG[1] = {0};")
    out.append("static const size_t SPLASH_JPG_LEN = 0;")
text = "\n".join(out) + "\n"
old = open(dst, encoding="utf-8").read() if os.path.exists(dst) else None
if old != text:  # nur bei Aenderung schreiben, sonst baut PlatformIO jedes Mal neu
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
print("Startbild: %s" % ("%s, %d Bytes" % (os.path.relpath(src, root), len(jpg)) if jpg else "keins"))
