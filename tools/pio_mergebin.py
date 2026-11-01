# pio_mergebin.py — extra_script PlatformIO (post:) : cible « mergebin » qui assemble
# bootloader + partitions + boot_app0 + application en UNE image à flasher à l'offset 0x0
# (flasheurs web : esptool-js, ESP Web Tools). Offsets lus dans la config du framework
# plutôt que codés en dur, pour suivre un changement de table de partitions.
import os
Import("env")

def merge_bin(source, target, env):
    out = os.path.join(env.subst("$BUILD_DIR"), "firmware-merged.bin")
    images = [(off, env.subst(path)) for off, path in env["FLASH_EXTRA_IMAGES"]]
    images.append((env.subst("$ESP32_APP_OFFSET"), env.subst("$BUILD_DIR/${PROGNAME}.bin")))
    # « keep » : bootloader et appli portent déjà mode/fréquence/taille (QIO, 4 Mo) posés par
    # le build ; les réécrire invaliderait le hash SHA-256 ajouté au bootloader
    cmd = ['"$PYTHONEXE"', '"%s"' % os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py"),
           "--chip", env.BoardConfig().get("build.mcu"), "merge_bin", "-o", '"%s"' % out,
           "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep"]
    for off, path in images:
        cmd += [off, '"%s"' % path]
    return env.Execute(" ".join(cmd))

env.AddCustomTarget(
    name="mergebin",
    dependencies="$BUILD_DIR/${PROGNAME}.bin",
    actions=merge_bin,
    title="Merge bin",
    description="Image unique firmware-merged.bin à flasher à 0x0",
)
