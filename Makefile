# ==========================================================================
# Makefile - Sistema Gestione Aula Studio (Traccia 2 - PSD 2025/2026)
# ==========================================================================
# Utilizzo:
#   make              → compila il programma principale
#   make run          → compila ed esegue il programma principale
#   make test         → compila il programma di test
#   make run-test     → compila ed esegue i test
#   make all          → compila entrambi (main + test)
#   make debug        → compila con flag AddressSanitizer per debug avanzato
#   make clean        → rimuove tutti i file generati
# ==========================================================================

# ── Rilevamento sistema operativo ─────────────────────────────────────────
ifeq ($(OS),Windows_NT)
    RM      = del /f /q
    EXT     = .exe
    RUN     = 
else
    RM      = rm -f
    EXT     =
    RUN     = ./
endif

# ── Variabili di compilazione ──────────────────────────────────────────────
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g
CFLAGS_DEBUG = -Wall -Wextra -std=c99 -g -fsanitize=address -fsanitize=undefined

# ── File sorgenti e target ─────────────────────────────────────────────────
SRCS_COMMON = funzioni.c
HEADERS     = funzioni.h strutture.h

TARGET      = aula_studio$(EXT)
OBJ_MAIN    = main.o funzioni.o

TEST_TARGET = test_aula$(EXT)
OBJ_TEST    = test.o funzioni.o

# ── Target di default: compila il programma principale ────────────────────
all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(OBJ_MAIN)
	$(CC) $(OBJ_MAIN) -o $(TARGET)
	@echo "[OK] Compilato: $(TARGET)"

$(TEST_TARGET): $(OBJ_TEST)
	$(CC) $(OBJ_TEST) -o $(TEST_TARGET)
	@echo "[OK] Compilato: $(TEST_TARGET)"

# ── Compilazione singoli moduli ────────────────────────────────────────────
main.o: main.c $(HEADERS)
	$(CC) $(CFLAGS) -c main.c

funzioni.o: funzioni.c $(HEADERS)
	$(CC) $(CFLAGS) -c funzioni.c

test.o: test.c $(HEADERS)
	$(CC) $(CFLAGS) -c test.c

# ── Esecuzione ─────────────────────────────────────────────────────────────
run: $(TARGET)
	@echo "[RUN] Avvio programma principale..."
	$(RUN)$(TARGET)

run-test: $(TEST_TARGET)
	@echo "[RUN] Avvio test suite..."
	$(RUN)$(TEST_TARGET)

# ── Debug con AddressSanitizer (rileva memory leak e accessi invalidi) ─────
debug: 
	$(CC) $(CFLAGS_DEBUG) $(SRCS_COMMON) main.c -o aula_debug$(EXT)
	@echo "[OK] Build debug: aula_debug$(EXT)"
	@echo "[RUN] Avvio in modalita' debug..."
	$(RUN)aula_debug$(EXT)

debug-test:
	$(CC) $(CFLAGS_DEBUG) $(SRCS_COMMON) test.c -o test_debug$(EXT)
	@echo "[OK] Build debug test: test_debug$(EXT)"
	@echo "[RUN] Avvio test in modalita' debug..."
	$(RUN)test_debug$(EXT)

# ── Pulizia ────────────────────────────────────────────────────────────────
clean:
	$(RM) *.o $(TARGET) $(TEST_TARGET) aula_debug$(EXT) test_debug$(EXT)
	@echo "[OK] Pulizia completata."

# ── Dichiarazioni phony (evita conflitti con file omonimi) ─────────────────
.PHONY: all run run-test debug debug-test clean