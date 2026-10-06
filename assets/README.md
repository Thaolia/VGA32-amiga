# assets/ — matériel Amiga (généré, NON committé)

Ce dossier reçoit les en-têtes embarquant **votre** matériel Amiga, légalement obtenu. Ils sont
**git-ignorés** (copyright) : ne jamais les committer ni les partager.

| Fichier | Généré par | Source |
|---|---|---|
| `kick_rom.h` | `python3 tools/make_kick_header.py <kick34005.A500> assets` | ROM Kickstart 1.3 (256 Ko, CRC32 `C4F0F55F`) |
| `wb_adf.h` | `python3 tools/make_adf_header.py <workbench13.adf> assets` | image disque ADF |

Le build (`platformio.ini`, `-Iassets`) cherche ces fichiers ici. Sans eux, la compilation
échoue sur `#include "kick_rom.h"`.

> Alternative pour l'ADF : le charger depuis une **carte SD** au runtime plutôt que de l'embarquer
> (voir `docs/PORTING.md`). Le Kickstart, lui, reste embarqué.
