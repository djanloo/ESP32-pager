"""Resetta la scheda (linee DTR/RTS) e stampa il log seriale per alcuni secondi.

Uso: serial_log.py [porta] [secondi]
"""

import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0

with serial.Serial(port, 115200, timeout=0.2) as s:
    s.dtr = False  # IO0 alto: avvio normale, non download mode
    s.rts = True   # EN basso: reset
    time.sleep(0.1)
    s.rts = False
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        data = s.read(1024)
        if data:
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()
