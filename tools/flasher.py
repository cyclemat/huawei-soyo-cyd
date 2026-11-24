#!/usr/bin/env python3
"""Startet den lokalen Web-Flasher: erzeugt Geraeteliste und Manifeste (tools/make_flasher.py), startet einen kleinen
Webserver nur fuer diesen Rechner (127.0.0.1) und oeffnet den Browser. Flashen per USB verlangt im Browser eine
sichere Adresse – http://localhost gilt als sicher, eine Datei per Doppelklick (file://) nicht.

  python tools/flasher.py            (oder Doppelklick auf flasher.bat im Projektordner)
Beenden: Fenster schliessen bzw. Strg+C.
"""
import functools
import http.server
import os
import socketserver
import subprocess
import sys
import threading
import webbrowser

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 8766   # 8765 nutzt der Flasher von Simple Open Camper

subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'make_flasher.py')], check=False)


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):   # leise: nur Fehler
        if args and str(args[1]).startswith(('4', '5')):
            sys.stderr.write('%s\n' % (fmt % args))

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


handler = functools.partial(Handler, directory=ROOT)
url = 'http://localhost:%d/flasher/' % PORT
try:
    srv = Server(('127.0.0.1', PORT), handler)
except OSError:
    print('Port %d ist belegt – läuft der Flasher schon? Öffne %s' % (PORT, url))
    webbrowser.open(url)
    sys.exit(0)
print('Flasher läuft: %s  (Beenden: Fenster schließen oder Strg+C)' % url)
threading.Timer(0.8, lambda: webbrowser.open(url)).start()
try:
    srv.serve_forever()
except KeyboardInterrupt:
    pass
