#!/usr/bin/env python3
"""
fetch_kickslopstart.py — récupère la dernière release de KickSlopStart (Kickstart 1.3
clean-room, licence MIT : https://github.com/Thaolia/KickSlopStart) et génère kick_rom.h.

Usage:
    python3 tools/fetch_kickslopstart.py [output_dir] [--tag vX.Y.Z]

Sortie (défaut : build/kss/, git-ignoré) :
    KickSlopStart.rom   la ROM (utilisable par tests/pc/a500)
    kick_rom.h          en-tête embarqué par les envs ttgo-vga32-kss*
    VERSION             tag + SHA-256 de la ROM en place

Sans réseau (ou API GitHub limitée), la ROM déjà en cache est conservée : le build ne
dépend d'Internet que la toute première fois. Variable KSS_TAG = équivalent de --tag.
"""
import sys, os, json, hashlib, zlib, urllib.request, urllib.error

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_kick_header import write_header

REPO = "Thaolia/KickSlopStart"
ROM_SIZE = 262144
ROM_MAGIC = bytes.fromhex("11114ef9")    # en-tête d'une ROM Kickstart 256 Ko
TIMEOUT = 15

def http_get(url, api=False):
    headers = {"User-Agent": "VGA32-amiga-build"}
    # CI : l'API anonyme est limitée à 60 requêtes/h par IP, partagée entre runners. Jeton
    # réservé à l'API : les téléchargements redirigent vers un stockage qui le refuserait.
    if api and os.environ.get("GITHUB_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["GITHUB_TOKEN"]
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return r.read()

def release_info(tag):
    url = (f"https://api.github.com/repos/{REPO}/releases/tags/{tag}" if tag
           else f"https://api.github.com/repos/{REPO}/releases/latest")
    rel = json.loads(http_get(url, api=True))
    roms = [a for a in rel.get("assets", []) if a["name"].lower().endswith(".rom")]
    if len(roms) != 1:
        raise RuntimeError(f"release {rel.get('tag_name')} : {len(roms)} asset .rom (1 attendu)")
    sums = [a for a in rel["assets"] if a["name"] == "SHA256SUMS"]
    return rel["tag_name"], roms[0], (sums[0] if sums else None)

def expected_sha(rom_asset, sums_asset):
    """SHA-256 attendu : digest de l'API ET fichier SHA256SUMS doivent concorder."""
    shas = set()
    digest = rom_asset.get("digest") or ""
    if digest.startswith("sha256:"):
        shas.add(digest[7:].lower())
    if sums_asset:
        for line in http_get(sums_asset["browser_download_url"]).decode().splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[1].lstrip("*") == rom_asset["name"]:
                shas.add(parts[0].lower())
    if len(shas) != 1:
        raise RuntimeError(f"empreintes SHA-256 absentes ou discordantes : {shas or 'aucune'}")
    return shas.pop()

def check_rom(rom):
    if len(rom) != ROM_SIZE:
        raise RuntimeError(f"taille {len(rom)} octets, {ROM_SIZE} attendus")
    if rom[:4] != ROM_MAGIC:
        raise RuntimeError(f"en-tête {rom[:4].hex()} != {ROM_MAGIC.hex()} (pas une ROM Kickstart 256 Ko)")

def read_cache(outdir):
    try:
        rom = open(os.path.join(outdir, "KickSlopStart.rom"), "rb").read()
        tag, sha = open(os.path.join(outdir, "VERSION")).read().split()
        check_rom(rom)
        if hashlib.sha256(rom).hexdigest() != sha:
            return None, None    # écriture interrompue : ROM et VERSION désaccordés
        return tag, rom
    except (OSError, ValueError, RuntimeError):
        return None, None

def main():
    args = sys.argv[1:]
    tag = os.environ.get("KSS_TAG") or None
    if "--tag" in args:
        i = args.index("--tag")
        tag = args[i + 1]
        del args[i:i + 2]
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    outdir = os.path.abspath(args[0] if args else os.path.join(root, "build", "kss"))
    os.makedirs(outdir, exist_ok=True)

    cached_tag, cached_rom = read_cache(outdir)
    try:
        rel_tag, rom_asset, sums_asset = release_info(tag)
        sha = expected_sha(rom_asset, sums_asset)
        if cached_rom is not None and hashlib.sha256(cached_rom).hexdigest() == sha:
            rom = cached_rom
            print(f"KickSlopStart {rel_tag} : déjà à jour")
        else:
            rom = http_get(rom_asset["browser_download_url"])
            got = hashlib.sha256(rom).hexdigest()
            if got != sha:
                raise RuntimeError(f"SHA-256 {got} != {sha} attendu")
            check_rom(rom)
            print(f"KickSlopStart {rel_tag} : téléchargé ({rom_asset['name']})")
    except (urllib.error.URLError, OSError, ValueError, KeyError, RuntimeError) as e:
        if cached_rom is None:
            print(f"ERREUR KickSlopStart : {e} — et aucune ROM en cache dans {outdir}")
            sys.exit(1)
        # une ROM corrompue ou falsifiée n'arrive jamais ici : seul le cache vérifié sert de repli
        print(f"ATTENTION KickSlopStart : {e} — ROM en cache {cached_tag} conservée")
        rel_tag, rom = cached_tag, cached_rom

    sha = hashlib.sha256(rom).hexdigest()
    version = f"{rel_tag} {sha}\n"
    header = os.path.join(outdir, "kick_rom.h")
    try:
        unchanged = (rom is cached_rom and os.path.exists(header)
                     and open(os.path.join(outdir, "VERSION")).read() == version)
    except OSError:
        unchanged = False
    if unchanged:
        return       # mtime de kick_rom.h préservé : pas de recompilation de main.cpp
    with open(os.path.join(outdir, "KickSlopStart.rom"), "wb") as f:
        f.write(rom)
    write_header(rom, outdir, zlib.crc32(rom) & 0xFFFFFFFF,
                 f"// kick_rom.h — KickSlopStart {rel_tag} (MIT, https://github.com/{REPO}).\n"
                 f"// Généré par tools/fetch_kickslopstart.py. SHA-256 {sha}\n")
    with open(os.path.join(outdir, "VERSION"), "w") as f:
        f.write(version)
    print(f"Écrit : {header}")

if __name__ == "__main__":
    main()
