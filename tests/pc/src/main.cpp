/* Runner Fase 0.
 * Uso:  a500 <kickstart.rom> [frame] [--trace N] [--tracefile out.txt]
 *   - esegue N frame PAL (default 100) a 455 cicli CPU/linea, 312 linee
 *   - --trace N: disassembla le prime N istruzioni (confronto col ground truth)
 *   - a fine corsa: PC corrente + istogramma degli ultimi PC (rileva stalli,
 *     stessa tecnica dello stallo 0x015B di Bubble Bobble)
 * Timing: 7.09MHz PAL, 709379/(50*312.5) = ~454 cicli/linea -> 455 arrotondato.
 * E-clock CIA = CPU/10.
 */
#include "a500.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
extern "C" {
#include "m68k.h"
}

void cia_tod_hsync(void);
void cia_tod_vsync(void);

#define LINES_PAL   312
#define CYC_LINE    455

int cur_frame = 0;

/* --pctrap: trappole sul PC con dump di stack (chiamante) e registri */
static uint32_t traps[4]; static int ntraps = 0; static int traphits = 0;
extern "C" void instr_hook(unsigned int pc)
{
    if (!ntraps || traphits >= 60) return;
    for (int i = 0; i < ntraps; i++)
        if (pc == traps[i]) {
            uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
            logmsg("[TRAP] f=%d PC=%06X ret=[%06X %06X %06X] D0=%08X A1=%08X\n",
                   cur_frame, pc,
                   m68k_read_memory_32(sp) & 0xFFFFFF,
                   m68k_read_memory_32(sp + 4) & 0xFFFFFF,
                   m68k_read_memory_32(sp + 8) & 0xFFFFFF,
                   m68k_get_reg(NULL, M68K_REG_D0),
                   m68k_get_reg(NULL, M68K_REG_A1));
            traphits++;
            return;
        }
}
static uint32_t pc_ring[256];
static int pc_ring_i = 0;

static uint32_t irq_count[8];

extern "C" int irq_ack(int level)
{
    if (level >= 1 && level <= 7) irq_count[level]++;
    return M68K_INT_ACK_AUTOVECTOR;
}

static void trace_instr(int n)
{
    char buf[128];
    for (int i = 0; i < n; i++) {
        uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
        m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
        logmsg("%06X: %s\n", pc, buf);
        m68k_execute(1);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: %s <kickstart.rom> [frame] [--trace N] [--tracefile f] [--dasm ADDRHEX N]\n", argv[0]);
        return 1;
    }
    int frames = 100, trace_n = 0;
    uint32_t dasm_addr = 0; int dasm_n = 0;
    const char *ppm_path = "screen.ppm";
    const char *ram_path = NULL;
    int bench = 0;
    uint32_t ramload_addr = 0; const char *ramload_path = NULL;
    const char *wav_path = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--tracefile") && i + 1 < argc) tracef = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm_path = argv[++i];
        else if (!strcmp(argv[i], "--ramdump") && i + 1 < argc) ram_path = argv[++i];
        else if (!strcmp(argv[i], "--blitlog") && i + 1 < argc) blitlogf = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "--rdlog") && i + 1 < argc) rdlogf = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "--disk") && i + 1 < argc) {
            if (drive_insert_adf(argv[++i]) < 0) return 1;
        }
        else if (!strcmp(argv[i], "--benchmark")) bench = 1;
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) {
            wav_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--ramload") && i + 2 < argc) {
            ramload_addr = (uint32_t)strtoul(argv[i+1], NULL, 0);
            ramload_path = argv[i+2];
            i += 2;
        }
        else if (!strcmp(argv[i], "--mousescript") && i + 1 < argc) {
            if (input_load_script(argv[++i]) < 0) return 1;
        }
        else if (!strcmp(argv[i], "--mfmdump") && i + 2 < argc) {
            int trk = atoi(argv[++i]);
            return disk_mfmdump(trk, argv[++i]);
        }
        else if (!strcmp(argv[i], "--pctrap") && i + 1 < argc) {
            char *tok = strtok(argv[++i], ",");
            while (tok && ntraps < 4) { traps[ntraps++] = (uint32_t)strtoul(tok, NULL, 16); tok = strtok(NULL, ","); }
        }
        else if (!strcmp(argv[i], "--dasm") && i + 2 < argc) {
            dasm_addr = (uint32_t)strtoul(argv[++i], NULL, 16);
            dasm_n = atoi(argv[++i]);
        }
        else frames = atoi(argv[i]);
    }

    if (mem_load_rom(argv[1]) < 0) return 1;
    mem_reset();
    paula_reset();
    if (ramload_path) {   /* pre-carica chip ram DOPO il reset che l'azzera */
        FILE *rf = fopen(ramload_path, "rb");
        if (rf) { extern uint8_t chip_ram[];
                  fread(chip_ram + (ramload_addr & 0x7FFFF), 1, 0x80000 - (ramload_addr & 0x7FFFF), rf);
                  fclose(rf); }
    }
    cia_reset();
    custom_reset();

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_set_int_ack_callback(irq_ack);

    if (dasm_n > 0) {   /* solo disassembly statico dalla ROM, nessuna esecuzione */
        char buf[128];
        uint32_t pc = dasm_addr;
        for (int i = 0; i < dasm_n; i++) {
            unsigned sz = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
            logmsg("%06X: %s\n", pc, buf);
            pc += sz;
        }
        return 0;
    }

    m68k_pulse_reset();

    /* checkpoint 1: vettori di reset letti dalla ROM in overlay */
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
    uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);
    logmsg("[RESET] SSP=%08X PC=%06X (atteso PC=FC00D2 per KS1.3 r34.5)\n", sp, pc);
    if (pc != 0xFC00D2)
        logmsg("[RESET] ATTENZIONE: PC diverso dall'atteso — verificare ROM/overlay\n");

    if (trace_n > 0) {
        logmsg("---- trace prime %d istruzioni ----\n", trace_n);
        trace_instr(trace_n);
        logmsg("---- fine trace ----\n");
    }

    int cur_frame_local = 0; (void)cur_frame_local;
    /* --- benchmark: accumulatori per sottosistema (ns) --- */
    long long t_cpu = 0, t_chip = 0, t_render = 0;
    struct timespec ta, tb;
    long long bench_total_ns = 0;
    struct timespec bench_start;
    if (bench) clock_gettime(CLOCK_MONOTONIC, &bench_start);

    /* Paula usa colorclock PAL (~3.546 MHz), mentre CYC_LINE e' in cicli CPU 68000
       (~7.09 MHz). Accumuliamo /2 senza perdere il mezzo ciclo delle linee dispari. */
    int paula_cc_acc = 0;

    for (int f = 0; f < frames; f++) {
        cur_frame = f;
        input_frame(f);
        copper_vblank();                 /* ricarica COP1LC a inizio frame */
        sprite_vblank();                 /* i canali sprite si riavviano */
        for (int line = 0; line < LINES_PAL; line++) {
            vpos = line;
            if (blit_irq_pending) { blit_irq_pending = 0; intreq_set(6); }
            if (disk_irq_pending) { disk_irq_pending = 0; intreq_set(1); }

            if (bench) clock_gettime(CLOCK_MONOTONIC, &ta);
            copper_run_line();           /* MOVE della copper list per questa linea */
            if (bench) { clock_gettime(CLOCK_MONOTONIC, &tb);
                t_chip += (tb.tv_sec-ta.tv_sec)*1000000000LL + (tb.tv_nsec-ta.tv_nsec); ta = tb; }

            denise_render_line();        /* rendering nel framebuffer */
            if (bench) { clock_gettime(CLOCK_MONOTONIC, &tb);
                t_render += (tb.tv_sec-ta.tv_sec)*1000000000LL + (tb.tv_nsec-ta.tv_nsec); ta = tb; }

            m68k_execute(CYC_LINE);
            if (bench) { clock_gettime(CLOCK_MONOTONIC, &tb);
                t_cpu += (tb.tv_sec-ta.tv_sec)*1000000000LL + (tb.tv_nsec-ta.tv_nsec); ta = tb; }

            paula_cc_acc += CYC_LINE;
            {
                int paula_cc = paula_cc_acc >> 1;
                paula_cc_acc &= 1;
                paula_step(paula_cc);        /* audio: Paula avanza in colorclock, non in cicli CPU */
            }
            cia_tick(CYC_LINE / 10);
            cia_tod_hsync();
            if (bench) { clock_gettime(CLOCK_MONOTONIC, &tb);
                t_chip += (tb.tv_sec-ta.tv_sec)*1000000000LL + (tb.tv_nsec-ta.tv_nsec); }

            pc_ring[pc_ring_i++ & 255] = m68k_get_reg(NULL, M68K_REG_PC);
        }
        cia_tod_vsync();
        intreq_set(5);   /* VERTB, livello 3: servito solo se INTENA lo abilita */
    }
    if (wav_path) paula_write_wav(wav_path);

    if (bench) {
        clock_gettime(CLOCK_MONOTONIC, &tb);
        bench_total_ns = (tb.tv_sec-bench_start.tv_sec)*1000000000LL + (tb.tv_nsec-bench_start.tv_nsec);
        double us_f = 1000.0 / frames;   /* ns totali -> us/frame: /1000 e /frames */
        long long t_blit_ns = blitter_ns();
        logmsg("\n==== BENCHMARK (%d frame PAL) ====\n", frames);
        logmsg("  CPU (Musashi)   : %8.1f us/frame  (%.1f%%)\n",
               t_cpu/1000.0/frames, 100.0*t_cpu/bench_total_ns);
        logmsg("  chipset+CIA     : %8.1f us/frame  (%.1f%%)\n",
               t_chip/1000.0/frames, 100.0*t_chip/bench_total_ns);
        logmsg("  blitter         : %8.1f us/frame  (%.1f%%)\n",
               t_blit_ns/1000.0/frames, 100.0*t_blit_ns/bench_total_ns);
        logmsg("  render (Denise) : %8.1f us/frame  (%.1f%%)\n",
               t_render/1000.0/frames, 100.0*t_render/bench_total_ns);
        logmsg("  ---------------------------------\n");
        logmsg("  TOTALE misurato : %8.1f us/frame\n", bench_total_ns/1000.0/frames);
        logmsg("  budget PAL 50Hz :   20000.0 us/frame (obiettivo real-time)\n");
        double ratio = (bench_total_ns/1000.0/frames) / 20000.0;
        logmsg("  => %.2fx del budget  (%s)\n", ratio,
               ratio <= 1.0 ? "real-time possibile su questo PC" : "oltre budget su questo PC");
        (void)us_f;
    }

    if (ram_path) {
        FILE *rf = fopen(ram_path, "wb");
        if (rf) { fwrite(chip_ram, 1, CHIP_SIZE, rf); fclose(rf);
                  logmsg("[RAM] chip RAM scritta in %s\n", ram_path); }
    }
    if (video_write_ppm(ppm_path) == 0)
        logmsg("[VID] ultimo frame scritto in %s\n", ppm_path);

    /* diagnostica finale: dove siamo e dove giravamo */
    logmsg("\n[FINE] %d frame eseguiti. PC=%06X SR=%04X ovl=%d\n",
           frames, m68k_get_reg(NULL, M68K_REG_PC),
           m68k_get_reg(NULL, M68K_REG_SR), ovl);
    logmsg("[FINE] INTENA=%04X INTREQ=%04X DMACON=%04X\n", intena, intreq, dmacon);
    logmsg("[FINE] blit richiesti: %u\n", blit_count);
    logmsg("[FINE] IRQ serviti per livello: L1=%u L2=%u L3=%u L4=%u L5=%u L6=%u L7=%u\n",
           irq_count[1], irq_count[2], irq_count[3], irq_count[4],
           irq_count[5], irq_count[6], irq_count[7]);

    /* ExecBase e LastAlert: distinguono boot pulito (LastAlert=FFFFFFFF)
       da Guru Meditation (LastAlert = codice alert, +0x206 = task) */
    uint32_t execbase = m68k_read_memory_32(4);
    if (!ovl && execbase >= 0x100 && execbase < CHIP_SIZE - 0x300) {
        uint32_t la  = m68k_read_memory_32(execbase + 0x202);
        uint32_t lat = m68k_read_memory_32(execbase + 0x206);
        logmsg("[FINE] ExecBase=%06X LastAlert=%08X task=%08X %s\n",
               execbase, la, lat,
               la == 0xFFFFFFFF ? "(nessun alert: boot pulito)" : "(** GURU **)");
    } else {
        logmsg("[FINE] ExecBase=%08X non valida (exec non inizializzato?)\n", execbase);
    }

    /* istogramma compatto degli ultimi 256 PC campionati (uno per linea) */
    uint32_t uniq[16]; int cnt[16], nu = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t p = pc_ring[i]; int j;
        for (j = 0; j < nu; j++) if (uniq[j] == p) { cnt[j]++; break; }
        if (j == nu && nu < 16) { uniq[nu] = p; cnt[nu] = 1; nu++; }
    }
    logmsg("[FINE] PC campionati (ultimi 256, 1/linea):\n");
    for (int j = 0; j < nu; j++) logmsg("  %06X x%d\n", uniq[j], cnt[j]);
    if (nu == 1) {
        logmsg("[FINE] possibile stallo/loop stretto a %06X — indagare\n", uniq[0]);
        uint32_t sr = m68k_get_reg(NULL, M68K_REG_SR);
        logmsg("[FINE] SR=%04X  IRQ mask=%d (se 7, blocca tutto tranne NMI)\n",
               sr, (sr >> 8) & 7);
        logmsg("[FINE] INTENA=%04X INTREQ=%04X DMACON=%04X\n",
               intena, intreq, dmacon);
        logmsg("[FINE]   vblank: %s%s  disco: %s\n",
               (intena&0x0020)?"ABIL ":"dis ", (intreq&0x0020)?"PEND":"-",
               (intreq&0x0002)?"DSKBLK-pend":"-");
        extern uint32_t intreq_src_count[16];
        logmsg("[FINE] INTREQ settati per sorgente (quante volte ognuno):\n");
        logmsg("[FINE]   DSKBLK(1)=%u  PORTS/CIA-A(3)=%u  VBLANK(5)=%u  BLIT(6)=%u\n",
               intreq_src_count[1], intreq_src_count[3], intreq_src_count[5], intreq_src_count[6]);
        logmsg("[FINE]   AUD(7-10)=%u/%u/%u/%u  DSKSYNC(12)=%u  EXTER/CIA-B(13)=%u\n",
               intreq_src_count[7], intreq_src_count[8], intreq_src_count[9], intreq_src_count[10],
               intreq_src_count[12], intreq_src_count[13]);
        cia_b_dump();   /* perche' il CIA-B non genera interrupt? */
    }

    /* posizione del puntatore: decodifico i control word dello sprite 0
       (intuition li riscrive a ogni movimento; e' la verita' su dove sta) */
    uint32_t sp0 = (((uint32_t)custom_get(0x120) << 16) | custom_get(0x122)) & 0x7FFFE;
    if (sp0) {
        uint16_t w0 = (uint16_t)(chip_ram[sp0] << 8 | chip_ram[sp0 + 1]);
        uint16_t w1 = (uint16_t)(chip_ram[sp0 + 2] << 8 | chip_ram[sp0 + 3]);
        int vstart = (w0 >> 8) | ((w1 & 4) << 6);
        int hstart = ((w0 & 0xFF) << 1) | (w1 & 1);
        logmsg("[FINE] sprite0 @%05X: vstart=%d hstart=%d -> puntatore schermo (%d,%d)\n",
               sp0, vstart, hstart, hstart - 0x81, vstart - 0x2C);
    } else {
        logmsg("[FINE] sprite0: puntatore non inizializzato (SPR0PT=0)\n");
    }

    /* dump della catena copper: parte da COP1LC e segue gli strobe COPJMP
       (l'installazione della view in graphics 1.3 e' incatenata via COP2LC) */
    uint32_t lc1 = ((uint32_t)custom_get(0x080) << 16 | custom_get(0x082)) & 0x7FFFE;
    uint32_t lc2 = ((uint32_t)custom_get(0x084) << 16 | custom_get(0x086)) & 0x7FFFE;
    logmsg("[FINE] COP1LC=%06X COP2LC=%06X — catena copper:\n", lc1, lc2);
    uint32_t clp = lc1;
    int jumps = 0;
    for (int i = 0; i < 64; i++) {
        uint16_t i1 = (uint16_t)(chip_ram[clp] << 8 | chip_ram[clp + 1]);
        uint16_t i2 = (uint16_t)(chip_ram[clp + 2] << 8 | chip_ram[clp + 3]);
        if (!(i1 & 1)) {
            uint32_t reg = i1 & 0x1FE;
            logmsg("  %06X: MOVE %03X,%04X\n", clp, reg, i2);
            if ((reg == 0x088 || reg == 0x08A) && jumps < 4) {
                clp = (reg == 0x088 ? lc1 : lc2);
                jumps++;
                logmsg("  ---- COPJMP%d -> %06X ----\n", reg == 0x088 ? 1 : 2, clp);
                continue;
            }
        } else {
            logmsg("  %06X: %s v=%02X h=%02X ve=%02X\n", clp,
                   (i2 & 1) ? "SKIP" : "WAIT", i1 >> 8, i1 & 0xFE, (i2 >> 8) & 0xFF);
            if (i1 == 0xFFFF && i2 == 0xFFFE) break;
        }
        clp = (clp + 4) & 0x7FFFE;
    }

    /* mappa zone non-zero della chip RAM (granularita 4KB) */
    logmsg("[FINE] chip RAM non-zero (blocchi 4KB): ");
    for (uint32_t b = 0; b < CHIP_SIZE; b += 0x1000) {
        int nz = 0;
        for (uint32_t i = 0; i < 0x1000; i++) if (chip_ram[b + i]) { nz = 1; break; }
        if (nz) logmsg("%02X ", b >> 12);
    }
    logmsg("\n");

    if (tracef) fclose(tracef);
    if (blitlogf) fclose(blitlogf);
    if (rdlogf) fclose(rdlogf);
    return 0;
}
