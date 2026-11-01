# pio_kickslopstart.py — extra_script PlatformIO (pre:) des envs ttgo-vga32-kss* :
# récupère la dernière release KickSlopStart à chaque build et fait passer build/kss/
# AVANT assets/ dans le chemin d'inclusion, pour que "kick_rom.h" désigne la ROM libre
# sans écraser l'en-tête Kickstart 1.3 de l'utilisateur.
import os, subprocess
Import("env")

proj = env.subst("$PROJECT_DIR")
outdir = os.path.join(proj, "build", "kss")
script = os.path.join(proj, "tools", "fetch_kickslopstart.py")
if subprocess.call([env.subst("$PYTHONEXE"), script, outdir]) != 0:
    env.Exit(1)
env.Prepend(CPPPATH=[outdir])
