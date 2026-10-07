/* musashi_tables.c — Écritures dans les tables d'opcodes de Musashi (en PSRAM).
 *
 * La bibliothèque Musashi (third_party/musashi) est compilée SANS -mfix-esp32-psram-cache-issue
 * (library.json, unflags) : son état (registres, drapeaux, PC) est en RAM interne et chaque mise à
 * jour y payait un memw inutile. Ses seules écritures en PSRAM sont le remplissage des tables de
 * saut et de cycles à l'init (m68ki_build_opcode_table) : elles passent par ces deux fonctions,
 * compilées avec le reste du projet, donc AVEC le contournement du défaut PSRAM de la puce rev1.
 * Les accès à la mémoire Amiga passent par src/core/memory.cpp, compilé lui aussi avec.
 */
extern void (**m68ki_instruction_jump_table)(void);
extern unsigned char (*m68ki_cycles)[0x10000];

void m68k_tbl_jump_set(unsigned op, void (*handler)(void))
{
    m68ki_instruction_jump_table[op] = handler;
}

void m68k_tbl_cycle_set(unsigned row, unsigned op, unsigned char cycles)
{
    m68ki_cycles[row][op] = cycles;
}
