#!/usr/bin/env python3
"""Sentinella di regressione del boot KS 1.3 (Fase 0 v1.0 come baseline).
Verifica gli invarianti di un run da 400 frame:
  1. reset corretto (PC=FC00D2, nessun warning)
  2. boot pulito (LastAlert = nessun alert)
  3. disegno completato: blit totali in range sano (non 0, non infiniti)
  4. sipario aperto: palette vera nella catena copper (COLOR01=0000)
  5. sistema a regime: PC campionati in zona poll/idle (strap+trackdisk+exec)
"""
import sys, re

txt = open(sys.argv[1] if len(sys.argv) > 1 else "boot_check.txt").read()
ok = True

def check(name, cond, extra=""):
    global ok
    ok &= bool(cond)
    print(f"{'OK ' if cond else 'FAIL'} {name}{' — ' + extra if extra and not cond else ''}")

check("reset PC=FC00D2", "PC=FC00D2 (atteso" in txt and "ATTENZIONE" not in txt)
check("boot pulito (no Guru)", "nessun alert: boot pulito" in txt)

m = re.search(r"blit richiesti: (\d+)", txt)
n = int(m.group(1)) if m else -1
check(f"blit completati ({n})", 800 <= n <= 2000,
      "disegno non completato o loop infinito")

check("palette rivelata (COLOR01=0000)", "MOVE 182,0000" in txt,
      "sipario ancora chiuso: COLOR01 non e' nero")
check("bitplane attivi nella lista (0E2,A572)", "MOVE 0E2,A572" in txt)
check("display DMA acceso (DMACON bit BPLEN)",
      bool(re.search(r"DMACON=0[37]D0", txt)))

pcs = re.findall(r"^  (F[CDE][0-9A-F]{4}) x\d+", txt, re.M)
zone = [p for p in pcs if p.startswith(("FE86", "FE9C", "FE9E", "FC07", "FC0F", "FE97"))]
check(f"regime poll/idle ({len(zone)}/{len(pcs)} PC in zona)", len(zone) >= len(pcs) // 2,
      "il sistema non e' nel loop di attesa disco")

sys.exit(0 if ok else 1)
