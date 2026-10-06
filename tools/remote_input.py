#!/usr/bin/env python3
"""
remote_input.py — Clavier + souris du PC -> Amiga émulé sur la TTGO VGA32, via l'UART USB.

Ouvre une petite fenêtre : un clic dedans CAPTURE clavier et souris, qui sont
envoyés à la carte en trames binaires (décodées par src/hal/serial_proto.cpp).
F12 libère la capture. Les logs série de la carte s'affichent dans ce terminal.

Les touches sont lues par POSITION physique (scancode USB HID, indépendant du
layout du PC) et traduites en rawcode Amiga ; c'est l'Amiga qui applique sa keymap.

Dépendances : pygame (>= 2) ou pygame-ce, + pyserial.
Usage :
    python3 tools/remote_input.py [--port /dev/ttyACM0] [--mouse-scale 1.0] [--no-log]

Format de trame : 0xA5 TYPE PAYLOAD... CHK  (CHK = somme 8 bits de TYPE+PAYLOAD)
    'K' : rawcode | 0x80 si relâchée
    'M' : dx int8, dy int8 (bas = +), boutons (b0 gauche, b1 droit)
"""
import argparse
import os
import sys
import threading
import time

SYNC = 0xA5
MOUSE_PERIOD = 0.02          # envoi souris à 50 Hz max (la carte tourne à 15-40 fps)
STEP = 127                   # |delta| max par trame (int8)

# Scancode USB HID (= SDL_Scancode) -> rawcode Amiga positionnel (HRM, cf. input_ps2.cpp)
HID_TO_AMIGA = {
    # lettres a..z (HID 4..29)
    **dict(zip(range(4, 30), [
        0x20, 0x35, 0x33, 0x22, 0x12, 0x23, 0x24, 0x25, 0x17, 0x26, 0x27, 0x28, 0x37,
        0x36, 0x18, 0x19, 0x10, 0x13, 0x21, 0x14, 0x16, 0x34, 0x11, 0x32, 0x15, 0x31])),
    # rangée chiffres 1..0 (HID 30..39)
    **dict(zip(range(30, 40), range(0x01, 0x0B))),
    40: 0x44,  # Entrée
    41: 0x45,  # Échap
    42: 0x41,  # Retour arrière
    43: 0x42,  # Tab
    44: 0x40,  # Espace
    45: 0x0B,  # -
    46: 0x0C,  # =
    47: 0x1A,  # [
    48: 0x1B,  # ]
    49: 0x0D,  # \
    50: 0x2B,  # touche ISO près d'Entrée (# ~) -> touche internationale Amiga
    51: 0x29,  # ;
    52: 0x2A,  # '
    53: 0x00,  # `
    54: 0x38,  # ,
    55: 0x39,  # .
    56: 0x3A,  # /
    57: 0x62,  # Verr. Maj.
    **dict(zip(range(58, 68), range(0x50, 0x5A))),   # F1..F10
    73: 0x5F,  # Inser -> HELP (l'Amiga n'a pas d'Inser)
    76: 0x46,  # Suppr
    79: 0x4E,  # droite
    80: 0x4F,  # gauche
    81: 0x4D,  # bas
    82: 0x4C,  # haut
    84: 0x5C,  # pavé /
    85: 0x5D,  # pavé *
    86: 0x4A,  # pavé -
    87: 0x5E,  # pavé +
    88: 0x43,  # pavé Entrée
    **dict(zip(range(89, 98), [0x1D, 0x1E, 0x1F, 0x2D, 0x2E, 0x2F, 0x3D, 0x3E, 0x3F])),
    98: 0x0F,  # pavé 0
    99: 0x3C,  # pavé .
    100: 0x30, # touche ISO < > (à gauche de Z) -> touche internationale Amiga
    224: 0x63, # Ctrl gauche
    225: 0x60, # Shift gauche
    226: 0x64, # Alt gauche
    227: 0x66, # Super gauche -> Amiga gauche
    228: 0x63, # Ctrl droit (l'Amiga n'a qu'un Ctrl)
    229: 0x61, # Shift droit
    230: 0x65, # Alt droit
    231: 0x67, # Super droit -> Amiga droite
}
HID_F12 = 69                 # libère la capture (pas de F12 sur Amiga)


def frame(ftype, payload):
    body = bytes([ord(ftype)]) + bytes(payload)
    return bytes([SYNC]) + body + bytes([sum(body) & 0xFF])


def encode_key(raw, down):
    return frame('K', [raw | (0 if down else 0x80)])


def encode_mouse(dx, dy, lmb, rmb):
    return frame('M', [dx & 0xFF, dy & 0xFF, (1 if lmb else 0) | (2 if rmb else 0)])


def open_port(path):
    import serial
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.timeout = 0.1
    # Linux active DTR+RTS ensemble à l'ouverture (sans effet sur l'auto-reset ESP32).
    # Les laisser actifs : pyserial baisserait DTR avant RTS, et l'instant « RTS seul »
    # tire EN à 0 -> la carte reboote à chaque connexion.
    s.dtr = True
    s.rts = True
    s.open()
    return s


def log_reader(ser, show, stop):
    while not stop.is_set():
        try:
            data = ser.read(4096)
        except Exception as e:          # carte débranchée : on le signale, sans planter la fenêtre
            print(f"[remote] lecture série interrompue : {e}", file=sys.stderr)
            return
        if data and show:
            sys.stdout.write(data.decode('utf-8', 'replace'))
            sys.stdout.flush()


class Remote:
    def __init__(self, ser, scale):
        self.ser = ser
        self.scale = scale
        self.held = set()        # rawcodes enfoncés (à relâcher si on perd la capture)
        self.lmb = self.rmb = False
        self.acc_x = self.acc_y = 0.0
        self.last_mouse = 0.0

    def send(self, data):
        try:
            self.ser.write(data)
        except Exception as e:
            print(f"[remote] écriture série impossible : {e}", file=sys.stderr)

    def key(self, scancode, down):
        raw = HID_TO_AMIGA.get(scancode)
        if raw is None:
            return
        if down:
            if raw in self.held:     # Ctrl G/D partagent 0x63 : pas de double appui
                return
            self.held.add(raw)
        else:
            if raw not in self.held:
                return
            self.held.discard(raw)
        self.send(encode_key(raw, down))

    def motion(self, dx, dy):
        self.acc_x += dx * self.scale
        self.acc_y += dy * self.scale

    def buttons(self, lmb, rmb):
        self.lmb, self.rmb = lmb, rmb
        self.flush_mouse(force=True)

    def flush_mouse(self, force=False):
        now = time.monotonic()
        if not force and now - self.last_mouse < MOUSE_PERIOD:
            return
        dx, dy = int(self.acc_x), int(self.acc_y)
        if not force and dx == 0 and dy == 0:
            return
        self.acc_x -= dx
        self.acc_y -= dy
        self.last_mouse = now
        while True:              # découpe en pas int8 ; la carte borne aussi par trame émulée
            sx = max(-STEP, min(STEP, dx))
            sy = max(-STEP, min(STEP, dy))
            self.send(encode_mouse(sx, sy, self.lmb, self.rmb))
            dx -= sx
            dy -= sy
            if dx == 0 and dy == 0:
                break

    def release_all(self):
        for raw in sorted(self.held):
            self.send(encode_key(raw, False))
        self.held.clear()
        self.acc_x = self.acc_y = 0.0
        if self.lmb or self.rmb:
            self.buttons(False, False)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--port', default='/dev/ttyACM0')
    ap.add_argument('--mouse-scale', type=float, default=1.0,
                    help='multiplicateur de vitesse souris (défaut 1.0)')
    ap.add_argument('--no-log', action='store_true', help="n'affiche pas les logs série de la carte")
    args = ap.parse_args()

    # avant l'init SDL : la capture prend aussi le clavier (Alt+Tab, Super... vont à l'Amiga)
    os.environ.setdefault('SDL_GRAB_KEYBOARD', '1')
    import pygame

    ser = open_port(args.port)
    stop = threading.Event()
    threading.Thread(target=log_reader, args=(ser, not args.no_log, stop), daemon=True).start()

    pygame.init()
    screen = pygame.display.set_mode((560, 120))
    pygame.display.set_caption('Amiga VGA32 — entrée distante')
    font = pygame.font.Font(None, 26)
    remote = Remote(ser, args.mouse_scale)
    captured = False

    def set_capture(on):
        nonlocal captured
        if not on:
            remote.release_all()
        captured = on
        # curseur caché + entrée capturée = mode souris relatif (pygame et pygame-ce) ;
        # set_relative_mode n'existe que dans pygame-ce, où il le rend explicite
        pygame.mouse.set_visible(not on)
        pygame.event.set_grab(on)
        if hasattr(pygame.mouse, 'set_relative_mode'):
            pygame.mouse.set_relative_mode(on)

    def draw():
        screen.fill((0, 85, 170) if captured else (40, 40, 40))
        lines = (["CAPTURÉ — clavier et souris vont à l'Amiga", "F12 pour libérer"]
                 if captured else
                 ["Cliquer ici pour capturer clavier + souris", f"port {args.port}"])
        for i, t in enumerate(lines):
            screen.blit(font.render(t, True, (255, 255, 255)), (16, 28 + 34 * i))
        pygame.display.flip()

    clock = pygame.time.Clock()
    draw()
    try:
        running = True
        while running:
            for ev in pygame.event.get():
                if ev.type == pygame.QUIT:
                    running = False
                elif not captured:
                    if ev.type == pygame.MOUSEBUTTONDOWN and ev.button == 1:
                        set_capture(True)
                        draw()
                elif ev.type == pygame.KEYDOWN:
                    if ev.scancode == HID_F12:
                        set_capture(False)
                        draw()
                    else:
                        remote.key(ev.scancode, True)
                elif ev.type == pygame.KEYUP:
                    remote.key(ev.scancode, False)
                elif ev.type == pygame.MOUSEMOTION:
                    remote.motion(*ev.rel)
                elif ev.type in (pygame.MOUSEBUTTONDOWN, pygame.MOUSEBUTTONUP):
                    down = ev.type == pygame.MOUSEBUTTONDOWN
                    if ev.button == 1:
                        remote.buttons(down, remote.rmb)
                    elif ev.button == 3:
                        remote.buttons(remote.lmb, down)
                elif ev.type == pygame.WINDOWFOCUSLOST:
                    set_capture(False)     # pas de touche « collée » côté Amiga
                    draw()
            if captured:
                remote.flush_mouse()
            clock.tick(250)
    finally:
        remote.release_all()
        stop.set()
        pygame.quit()
        ser.close()


if __name__ == '__main__':
    main()
