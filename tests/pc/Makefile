# a500_phase0 — build offline (g++ -Wall -Wextra come da disciplina di progetto)
# Musashi configurato 68000 puro via -D (sorgenti Musashi non modificati).
# Cœur chipset (../../src/core) et Musashi (../../third_party/musashi) : compilés directement
# depuis les sources de la cible (source unique) ; seuls src/main.cpp et src/hook_decl.h sont
# propres au harnais.
CXX      = g++
CC       = gcc
M68KCFG  = -DM68K_EMULATE_010=M68K_OPT_OFF -DM68K_EMULATE_020=M68K_OPT_OFF \
           -DM68K_EMULATE_040=M68K_OPT_OFF -DM68K_EMULATE_EC020=M68K_OPT_OFF \
           -DM68K_EMULATE_030=M68K_OPT_OFF \
           -DM68K_EMULATE_INT_ACK=M68K_OPT_ON \
           '-DM68K_INT_ACK_CALLBACK(A)=irq_ack(A)' \
           -DM68K_INSTRUCTION_HOOK=M68K_OPT_SPECIFY_HANDLER \
           '-DM68K_INSTRUCTION_CALLBACK(pc)=instr_hook(pc)'
M68KDIR  = ../../third_party/musashi
CFLAGS   = -O2 -I$(M68KDIR) -include src/hook_decl.h $(M68KCFG)
CXXFLAGS = $(CFLAGS) -Wall -Wextra
CORE     = ../../src/core
CORE_SRC = memory cia custom drive video blitter disk input paula
MUSASHI  = obj/m68k/m68kcpu.o obj/m68k/m68kops.o obj/m68k/m68kdasm.o obj/m68k/softfloat/softfloat.o
HAL      = ../../src/hal
OBJS     = obj/main.o $(CORE_SRC:%=obj/%.o) obj/zorro.o obj/musashi_tables.o

a500: $(OBJS) $(MUSASHI)
	$(CXX) -o $@ $^

# Musashi compilato come C, senza -Wextra (warning di terze parti non nostri)
obj/m68k/%.o: $(M68KDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

obj/main.o: src/main.cpp $(CORE)/a500.h | obj
	$(CXX) $(CXXFLAGS) -I$(CORE) -I$(HAL) -c -o $@ $<

obj/%.o: $(CORE)/%.cpp $(CORE)/a500.h | obj
	$(CXX) $(CXXFLAGS) -I$(CORE) -I$(HAL) -c -o $@ $<

# écritures des tables Musashi (compilées hors de la bibliothèque, voir le fichier)
obj/musashi_tables.o: $(HAL)/musashi_tables.c | obj
	$(CC) -O2 -c -o $@ $<

# module pur de la couche HAL utilisé par le cœur (Fast RAM Zorro II, aucune carte sur PC)
obj/zorro.o: $(HAL)/zorro.cpp $(HAL)/zorro.h | obj
	$(CXX) $(CXXFLAGS) -I$(HAL) -c -o $@ $<

obj:
	mkdir -p obj

test: a500 test/testrom.bin
	./a500 test/testrom.bin 2 --trace 12

test/testrom.bin: test/mkrom.py
	python3 test/mkrom.py $@

clean:
	rm -rf a500 obj tjoy tdisk tpaula tblitdiff test_blit.bin tdenise test_denise.ppm test/testrom.bin

.PHONY: test testvideo testblit testboot testmfm clean

testvideo: a500
	python3 test/mkrom_video.py test/testrom_video.bin
	./a500 test/testrom_video.bin 3 --ppm test_video.ppm > /dev/null
	python3 test/check_video.py test_video.ppm

testblit: a500
	python3 test/mkrom_blit.py test/testrom_blit.bin
	./a500 test/testrom_blit.bin 2 --ramdump blit.ram > /dev/null
	python3 test/check_blit.py blit.ram

testboot: a500
	@test -f kick34005.A500 || (echo "ERRORE: kick34005.A500 non presente nella dir progetto"; exit 1)
	./a500 kick34005.A500 400 > boot_check.txt 2>&1
	python3 test/check_boot.py boot_check.txt

testmfm: a500
	@test -f wb13.adf || (echo "ERRORE: wb13.adf non presente nella dir progetto"; exit 1)
	./a500 test/testrom.bin --disk wb13.adf --mfmdump 0 trk0.mfm > /dev/null 2>&1
	python3 test/check_mfm.py trk0.mfm wb13.adf 0

testsprite: a500
	python3 test/mkrom_sprite.py test/testrom_sprite.bin
	./a500 test/testrom_sprite.bin 5 --ppm test_sprite.ppm > /dev/null
	python3 test/check_sprite.py test_sprite.ppm

testscroll: a500
	python3 test/mkrom_scroll.py test/testrom_scroll.bin
	./a500 test/testrom_scroll.bin 2 --ramload 0x50000 test/testrom_scroll_pat.bin --ppm test_scroll.ppm > /dev/null
	python3 test/check_scroll.py test_scroll.ppm

testjoy: 
	g++ -I$(CORE) -o tjoy test/test_joy_c.cpp $(CORE)/input.cpp
	./tjoy
	python3 test/check_joy.py

testdisk:
	g++ -I$(CORE) -o tdisk test/test_disk_c.cpp $(CORE)/disk.cpp
	./tdisk

# Paula : WAV d'un scénario aléatoire figé, comparé à l'empreinte de l'implémentation d'origine
# (avance color clock par color clock, avant le pilotage par événements).
PAULA_REF_MD5 = 842c9f98f76d43720f661151d69afc31
testpaula:
	g++ -O2 -I$(CORE) -o tpaula test/test_paula_c.cpp $(CORE)/paula.cpp
	./tpaula test_paula.wav
	@test "$$(md5sum test_paula.wav | cut -d' ' -f1)" = "$(PAULA_REF_MD5)" && echo "OK  paula : WAV identique a la reference" || (echo "FAIL paula : WAV different de la reference"; exit 1)

# Blitter : 3000 blits aléatoires figés, chip RAM + registres comparés à l'empreinte de
# l'implémentation d'origine (avant minterm sans branchement / accès 16 bits / fill par table).
BLIT_REF_MD5 = 86299a0228d76f048bc7184db08209da
testblitdiff:
	g++ -O2 -I$(CORE) -I$(M68KDIR) -o tblitdiff test/test_blit_c.cpp $(CORE)/blitter.cpp
	./tblitdiff test_blit.bin > /dev/null
	@test "$$(md5sum test_blit.bin | cut -d' ' -f1)" = "$(BLIT_REF_MD5)" && echo "OK  blitter : resultat identique a la reference" || (echo "FAIL blitter : resultat different de la reference"; exit 1)

# Denise : 120 trames aléatoires (lores/hires, 0-6 plans, scroll), empreinte des framebuffers
# comparée à celle de l'implémentation d'origine (calcul bit par bit, avant tables c2p).
DENISE_REF = b6991587a7e58769
testdenise:
	g++ -O2 -I$(CORE) -o tdenise test/test_denise_c.cpp $(CORE)/video.cpp
	@test "$$(./tdenise)" = "$(DENISE_REF)" && echo "OK  denise : framebuffers identiques a la reference" || (echo "FAIL denise : framebuffers differents de la reference"; exit 1)

testaudio: a500
	python3 test/mkrom_audio.py test/testrom_audio.bin
	./a500 test/testrom_audio.bin 3 --ramload 0x40000 test/testrom_audio_pat.bin --wav test_audio.wav > /dev/null
	python3 test/check_audio.py test_audio.wav
