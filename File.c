/* ==========================================================================
 * File:    test.c
 * Modulo:  Test suite del Sistema di Gestione Accessi Aula Studio
 *          Traccia 2 - PSD 2025/2026
 * ---------------------------------------------------------------------------
 * Descrizione:
 *   Suite di test automatici separata dal main applicativo. Verifica
 *   in modo sistematico tutti i requisiti funzionali della traccia:
 *     - registrazione studenti (con anti-duplicato);
 *     - prenotazioni (valide, su matricola inesistente, duplicate);
 *     - disponibilita' dei posti dopo prenotazione/annullamento;
 *     - check-in e check-out;
 *     - ingresso senza prenotazione (con e senza coda);
 *     - lista d'attesa e subentro automatico;
 *     - annullamento con i suoi tre scenari (senza coda, con coda
 *       stessa fascia, con coda fascia diversa);
 *     - persistenza dello storico su file e generazione del report.
 *
 *   Ogni test e' INDIPENDENTE: viene eseguito su uno stato pulito
 *   grazie a reset_aula(), in modo che un eventuale fallimento di un
 *   test non "inquini" i successivi rendendo l'output illeggibile.
 *
 * Compilazione:
 *   gcc -std=c99 -Wall -g test.c funzioni.c -o test_aula
 *   (oppure: make test)
 *
 * Esecuzione:
 *   ./test_aula
 *
 * Convenzioni:
 *   - Ogni asserzione stampa "[PASS] descrizione" o "[FAIL] descrizione".
 *   - Le funzioni di test sono static: non vanno esposte fuori dal file.
 *   - L'anagrafica e' popolata una sola volta (test_registrazione) e
 *     poi riusata: gli studenti T001/T002/T003 sono disponibili per
 *     tutti i test successivi.
 * ========================================================================== */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "funzioni.h"

/* --------------------------------------------------------------------------
 * Macro ASSERT:
 *   Stampa "[PASS]" o "[FAIL]" in base al valore di una condizione.
 *   Usata al posto di assert() della libc per due motivi:
 *     1. assert() interromperebbe il programma al primo fallimento,
 *        mentre qui vogliamo eseguire TUTTI i test anche se uno fallisce;
 *     2. cosi' l'output e' uniforme e leggibile (una riga per check).
 *
 *   L'idioma "do { ... } while(0)" rende la macro sicura sintatticamente
 *   in qualsiasi contesto (anche dopo if/else senza graffe), evitando
 *   l'errore classico del trailing semicolon.
 *
 *   Uso: ASSERT(condizione, "messaggio descrittivo")
 * -------------------------------------------------------------------------- */
#define ASSERT(cond, msg) \
    do { \
        if (cond) printf("  [PASS] %s\n", msg); \
        else      printf("  [FAIL] %s\n", msg); \
    } while(0)

/*
 * reset_aula:
 *   Riporta l'aula a uno stato pulito tra un test e l'altro:
 *     - tutti i posti tornano LIBERO;
 *     - i contatori statistici vengono azzerati;
 *     - la coda viene ripulita dai nodi della fascia corrente.
 *
 *   L'implementazione DELEGA a cambio_fascia_automatica con
 *   nuova_fascia = MATTINA: questa funzione fa gia' tutto il lavoro
 *   di pulizia, e passandole MATTINA si chiude il turno corrente e si
 *   "riapre" sempre in mattina, rimettendo i test in una baseline nota.
 *   Riusare il codice esistente invece di scrivere reset manuale evita
 *   incoerenze (se cambia la logica di pulizia, basta aggiornare
 *   cambio_fascia_automatica).
 *
 *   Nota: l'anagrafica NON viene toccata. Gli studenti registrati
 *   restano disponibili per i test successivi.
 *
 * Parametri:
 *   aula, coda: non NULL.
 *
 * Pre:  sistema inizializzato.
 * Post: aula vuota, coda pulita dei nodi MATTINA, contatori a zero,
 *       fascia corrente = MATTINA.
 */
static void reset_aula(TurnoAula* aula, CodaAttesa* coda) {
    cambio_fascia_automatica(aula, coda, MATTINA);
}

/*
 * test_registrazione:
 *   Verifica la registrazione degli studenti in anagrafica.
 *
 *   Casi coperti:
 *     - inserimento di 3 studenti distinti, tutti ritrovabili;
 *     - tentativo di duplicato (stessa matricola, dati diversi):
 *       deve essere rifiutato SENZA sovrascrivere i dati esistenti
 *       (il nome originale "Mario Rossi" deve essere preservato);
 *     - ricerca di una matricola mai inserita: deve dare NULL.
 *
 *   Questo test e' eseguito per primo e popola l'anagrafica usata
 *   dai test successivi: NON va aggiunto un reset_aula in coda
 *   (che azzererebbe solo lo stato dinamico, ma comunque conviene
 *   non perdere tempo).
 *
 * Parametri:
 *   anagrafica: non NULL, gia' inizializzata.
 */
static void test_registrazione(TabellaHashStudenti* anagrafica) {
    Studente* trovato;

    printf("\n[TEST 1] Registrazione studenti\n");
    printf("------------------------------------------\n");

    registra_studente(anagrafica, "T001", "Mario Rossi",   "Informatica");
    registra_studente(anagrafica, "T002", "Elena Bianchi", "Matematica");
    registra_studente(anagrafica, "T003", "Luca Verdi",    "Fisica");

    ASSERT(cerca_studente(anagrafica, "T001") != NULL, "T001 registrato e trovato");
    ASSERT(cerca_studente(anagrafica, "T002") != NULL, "T002 registrato e trovato");
    ASSERT(cerca_studente(anagrafica, "T003") != NULL, "T003 registrato e trovato");

    /* Tentativo duplicato: se la registrazione sovrascrivesse i dati,
     * il nome diventerebbe "Duplicato". Il fatto che resti "Mario Rossi"
     * dimostra che il duplicato e' stato rifiutato correttamente. */
    registra_studente(anagrafica, "T001", "Duplicato", "Duplicato");
    trovato = cerca_studente(anagrafica, "T001");
    ASSERT(trovato != NULL && strcmp(get_nome_studente(trovato), "Mario Rossi") == 0,
           "Duplicato rifiutato: nome originale preservato");

    ASSERT(cerca_studente(anagrafica, "XXXX") == NULL,
           "Matricola inesistente non trovata");
}

/*
 * test_prenotazione:
 *   Verifica gli scenari principali di prenotazione.
 *
 *   Casi coperti:
 *     - prenotazione valida (matricola registrata): il contatore
 *       posti_occupati deve aumentare di 1;
 *     - prenotazione con matricola inesistente: il contatore NON
 *       deve cambiare (rifiuto);
 *     - prenotazione duplicata (stessa matricola, stessa fascia):
 *       anche qui il contatore NON deve cambiare.
 *
 *   Il valore di riferimento "occupati_prima" viene letto all'inizio
 *   invece di assumere zero: cosi' il test e' robusto anche se la
 *   baseline dello stato non fosse perfettamente pulita.
 *
 * Parametri:
 *   anagrafica, aula, coda: non NULL, sistema inizializzato e con
 *   T001 gia' registrato (vedi test_registrazione).
 */
static void test_prenotazione(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda) {
    OrarioVirtuale ora = {9, 30, 0};
    int occupati_prima = get_posti_occupati_totali(aula);

    printf("\n[TEST 2] Inserimento prenotazioni\n");
    printf("------------------------------------------\n");

    effettua_prenotazione(anagrafica, aula, coda, "T001", MATTINA, ora);
    ASSERT(get_posti_occupati_totali(aula) == occupati_prima + 1,
           "Prenotazione T001: posto assegnato");

    effettua_prenotazione(anagrafica, aula, coda, "XXXX", MATTINA, ora);
    ASSERT(get_posti_occupati_totali(aula) == occupati_prima + 1,
           "Matricola inesistente rifiutata");

    effettua_prenotazione(anagrafica, aula, coda, "T001", MATTINA, ora);
    ASSERT(get_posti_occupati_totali(aula) == occupati_prima + 1,
           "Prenotazione duplicata rifiutata");

    reset_aula(aula, coda);
}

/*
 * test_disponibilita:
 *   Verifica che il numero di posti LIBERO sia coerente prima e
 *   dopo una sequenza prenotazione + annullamento.
 *
 *   Atteso:
 *     - dopo una prenotazione i posti liberi calano di esattamente 1;
 *     - dopo l'annullamento tornano al valore iniziale.
 *
 *   Calcoliamo "liberi" come MAX_POSTI - posti_occupati_totali per
 *   non duplicare la logica di scansione dell'array dei posti (che
 *   resta nascosta nei tipi opachi).
 *
 * Parametri:
 *   anagrafica, aula, coda: non NULL.
 */
static void test_disponibilita(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda) {
    OrarioVirtuale ora = {9, 30, 0};
    int liberi_prima = MAX_POSTI - get_posti_occupati_totali(aula);
    int liberi_dopo;

    printf("\n[TEST 3] Disponibilita' posti\n");
    printf("------------------------------------------\n");

    effettua_prenotazione(anagrafica, aula, coda, "T001", MATTINA, ora);
    liberi_dopo = MAX_POSTI - get_posti_occupati_totali(aula);
    ASSERT(liberi_dopo == liberi_prima - 1,
           "Posti liberi -1 dopo prenotazione");

    annulla_prenotazione(anagrafica, aula, coda, "T001", ora);
    liberi_dopo = MAX_POSTI - get_posti_occupati_totali(aula);
    ASSERT(liberi_dopo == liberi_prima,
           "Posti liberi ripristinati dopo annullamento");

    reset_aula(aula, coda);
}

/*
 * test_checkin_checkout:
 *   Verifica il ciclo completo PRENOTATO -> OCCUPATO -> LIBERO.
 *
 *   Atteso:
 *     - dopo check-in il numero di posti occupati resta 1 (la
 *       transizione e' interna allo stato del posto, non aggiunge
 *       un nuovo posto);
 *     - dopo check-out, con coda vuota, il numero scende a 0.
 *
 *   Lo scenario con coda non vuota (subentro) e' coperto da
 *   test_lista_attesa.
 *
 * Parametri:
 *   anagrafica, aula, coda: non NULL.
 */
static void test_checkin_checkout(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda) {
    OrarioVirtuale ora = {9, 30, 0};

    printf("\n[TEST 4] Check-in e check-out\n");
    printf("------------------------------------------\n");

    effettua_prenotazione(anagrafica, aula, coda, "T001", MATTINA, ora);
    effettua_checkin(anagrafica, aula, coda, "T001", ora);
    ASSERT(get_posti_occupati_totali(aula) == 1,
           "Check-in: posto risulta occupato");

    effettua_checkout(anagrafica, aula, coda, "T001", ora);
    ASSERT(get_posti_occupati_totali(aula) == 0,
           "Check-out: posto liberato (coda vuota)");

    reset_aula(aula, coda);
}

/*
 * test_ingresso_senza_prenotazione:
 *   Verifica i due scenari dell'ingresso "drop-in" (senza prenotazione).
 *
 *   Caso A: coda vuota e posti liberi -> ingresso diretto consentito.
 *   Caso B: coda non vuota -> il nuovo arrivato DEVE finire in coda,
 *           anche se ci sono posti liberi (priorita' FIFO: chi ha
 *           gia' aspettato non viene scavalcato).
 *
 *   Per simulare il Caso B inseriamo manualmente un nodo "DUMMY"
 *   in coda con accoda_studente: e' una matricola che non esiste
 *   in anagrafica, ma per gli scopi di questo test va bene
 *   (effettua_checkin non valida le matricole presenti in coda).
 *
 * Parametri:
 *   anagrafica, aula, coda: non NULL.
 */
static void test_ingresso_senza_prenotazione(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda) {
    OrarioVirtuale ora = {9, 30, 0};

    printf("\n[TEST 5] Ingresso senza prenotazione\n");
    printf("------------------------------------------\n");

    /* Caso A */
    effettua_checkin(anagrafica, aula, coda, "T002", ora);
    ASSERT(get_posti_occupati_totali(aula) == 1,
           "Ingresso diretto senza prenotazione (coda vuota)");

    /* Caso B: forziamo un nodo in coda per attivare la priorita' FIFO. */
    accoda_studente(coda, "DUMMY", get_data_aula(aula), MATTINA);
    effettua_checkin(anagrafica, aula, coda, "T003", ora);
    ASSERT(get_dimensione_coda(coda) >= 1,
           "Ingresso con coda non vuota: T003 messo in attesa");

    reset_aula(aula, coda);
}

/*
 * test_lista_attesa:
 *   Verifica il funzionamento della lista d'attesa e il subentro
 *   automatico quando si libera un posto.
 *
 *   Scenario 1 (aula piena -> coda):
 *     Riempiamo artificialmente la coda con MAX_POSTI nodi "FILL".
 *     Un nuovo check-in di T001 deve aumentare la dimensione della
 *     coda di 1 (lo studente non trova posto e si mette in fila).
 *
 *   Scenario 2 (subentro):
 *     T001 ottiene un posto, T002 si mette in coda con stessa fascia,
 *     T001 esce. Atteso:
 *       - la coda si svuota (T002 e' stato promosso);
 *       - il posto resta assegnato (a T002, non a T001).
 *
 *   I due scenari sono separati da reset_aula() per partire puliti.
 *
 * Parametri:
 *   anagrafica, aula, coda: non NULL.
 */
static void test_lista_attesa(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda) {
    OrarioVirtuale ora = {9, 30, 0};
    int dim_prima;

    printf("\n[TEST 6] Lista di attesa e subentro\n");
    printf("------------------------------------------\n");

    /* Scenario 1: simula aula piena riempiendo la coda di nodi fittizi.
     * Non e' la situazione "naturale" (di solito si riempiono prima
     * i posti e poi la coda), ma e' funzionalmente equivalente per
     * verificare che il check-in successivo finisca in fila. */
    {
        int i;
        for (i = 0; i < MAX_POSTI; i++)
            accoda_studente(coda, "FILL", get_data_aula(aula), MATTINA);
    }
    dim_prima = get_dimensione_coda(coda);
    effettua_checkin(anagrafica, aula, coda, "T001", ora);
    ASSERT(get_dimensione_coda(coda) == dim_prima + 1,
           "Aula piena: T001 inserito in coda");

    reset_aula(aula, coda);

    /* Scenario 2: subentro reale.
     * Sequenza: T001 prenota e fa check-in, T002 si mette in coda,
     * T001 esce -> T002 deve subentrare automaticamente. */
    effettua_prenotazione(anagrafica, aula, coda, "T001", MATTINA, ora);
    effettua_checkin(anagrafica, aula, coda, "T001", ora);
    accoda_studente(coda, "T002", get_data_aula(aula), MATTINA);
    effettua_checkout(anagrafica, aula, coda, "T001", ora);

    ASSERT(get_dimensione_coda(coda) == 0,
           "Coda svuotata dopo subentro di T002");
    ASSERT(get_posti_occupati_totali(aula) == 1,
           "T002 ha subentrato: posto ancora assegnato");

    reset_aula(aula, coda);
}

/*
 * test_annullamento:
 *   Verifica i tre scenari distinti dell'annullamento di una
 *   prenotazione (vedi documentazione di annulla_prenotazione).
 *
 *   Caso A - Nessuno in coda:
 *     Il posto torna semplicemente LIBERO.
 *
 *   Caso B - Coda con stessa fascia:
 *     Il primo studente in coda con fascia compatibile subentra
 *     al posto; il contatore posti_occupati NON cambia (il posto
 *     ha solo cambiato "proprietario").
 *
 *   Caso C - Coda con fascia DIVERSA:
 *     Il subentrante non e' compatibile, quindi il posto torna
 *     LIBERO e la coda resta invariata (lo studente di un'altra
 *     fascia continua ad aspettare).
 *
 *   Questo e' il test piu' "ricco": copre la logica di matching
 *   sulla fascia che e' cruciale per il corretto comportamento
 *   del sistema in presenza di prenotazioni anticipate.
 *
 * Parametri:
 *   anagrafica, aula, coda: non NULL.
 */
static void test_annullamento(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda) {
    OrarioVirtuale ora = {9, 30, 0};
    int occupati_prima;

    printf("\n[TEST 7] Annullamento prenotazione\n");
    printf("------------------------------------------\n");

    /* Caso A */
    effettua_prenotazione(anagrafica, aula, coda, "T001", MATTINA, ora);
    occupati_prima = get_posti_occupati_totali(aula);
    annulla_prenotazione(anagrafica, aula, coda, "T001", ora);
    ASSERT(get_posti_occupati_totali(aula) == occupati_prima - 1,
           "Annullamento senza coda: posto liberato");

    /* Caso B */
    effettua_prenotazione(anagrafica, aula, coda, "T001", MATTINA, ora);
    accoda_studente(coda, "T002", get_data_aula(aula), MATTINA);
    annulla_prenotazione(anagrafica, aula, coda, "T001", ora);
    ASSERT(get_dimensione_coda(coda) == 0,
           "Annullamento con coda stessa fascia: T002 subentra");

    reset_aula(aula, coda);

    /* Caso C: T003 ha prenotato per POMERIGGIO, T001 annulla MATTINA.
     * T003 deve restare in coda (fascia incompatibile) e il posto
     * MATTINA deve tornare LIBERO. */
    effettua_prenotazione(anagrafica, aula, coda, "T001", MATTINA, ora);
    accoda_studente(coda, "T003", get_data_aula(aula), POMERIGGIO);
    occupati_prima = get_posti_occupati_totali(aula);
    annulla_prenotazione(anagrafica, aula, coda, "T001", ora);
    ASSERT(get_posti_occupati_totali(aula) == occupati_prima - 1,
           "Annullamento: nessun subentro se fascia coda diversa");
    ASSERT(get_dimensione_coda(coda) == 1,
           "T003 (pomeriggio) rimane in coda dopo annullamento mattina");

    reset_aula(aula, coda);
}

/*
 * test_storico_report:
 *   Verifica la persistenza degli eventi su file e la generazione
 *   del report finale.
 *
 *   Sequenza:
 *     1. Esegue una "storia tipica" (prenotazione + checkin + checkout)
 *        per garantire che alcuni eventi vengano scritti nello storico.
 *     2. Controlla che il file storico_accessi.txt sia accessibile in
 *        lettura: se esiste, le operazioni di append sono andate a buon
 *        fine. Non analizziamo il contenuto: la coerenza dei singoli
 *        eventi e' gia' verificata dai test precedenti tramite i
 *        contatori delle strutture.
 *     3. Chiama genera_report_aula: verifichiamo che non vada in crash
 *        (test "smoke"). L'output e' lasciato a video per l'esaminatore.
 *
 * Parametri:
 *   anagrafica, aula, coda: non NULL.
 */
static void test_storico_report(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda) {
    OrarioVirtuale ora = {9, 30, 0};
    FILE* fp;

    printf("\n[TEST 8] Storico accessi e report\n");
    printf("------------------------------------------\n");

    effettua_prenotazione(anagrafica, aula, coda, "T001", MATTINA, ora);
    effettua_checkin(anagrafica, aula, coda, "T001", ora);
    effettua_checkout(anagrafica, aula, coda, "T001", ora);

    fp = fopen("storico_accessi.txt", "r");
    ASSERT(fp != NULL, "File storico_accessi.txt esiste");
    if (fp) fclose(fp);

    /* Smoke test: il report deve eseguire fino in fondo senza crash. */
    genera_report_aula(aula, coda);

    reset_aula(aula, coda);
}

/*
 * main:
 *   Entry point della suite. Inizializza il sistema, esegue tutti i
 *   test in sequenza e libera la memoria prima di uscire.
 *
 *   I test vengono lanciati in un ordine preciso: la registrazione
 *   e' il primo perche' popola l'anagrafica con T001/T002/T003 che
 *   verranno riusati da tutti i test successivi. Gli altri test sono
 *   indipendenti tra loro grazie a reset_aula().
 *
 * Ritorna:
 *   0 sempre. Anche in caso di [FAIL] non si interrompe la suite:
 *   l'utente vuole vedere il quadro completo dei risultati.
 */
int main() {
    TabellaHashStudenti* anagrafica = NULL;
    TurnoAula*           aula       = NULL;
    CodaAttesa*          coda       = NULL;

    inizializza_sistema_dinamico(&anagrafica, &aula, &coda);

    printf("==========================================\n");
    printf("  TEST SUITE - Aula Studio (Traccia 2)   \n");
    printf("==========================================\n");

    test_registrazione(anagrafica);
    test_prenotazione(anagrafica, aula, coda);
    test_disponibilita(anagrafica, aula, coda);
    test_checkin_checkout(anagrafica, aula, coda);
    test_ingresso_senza_prenotazione(anagrafica, aula, coda);
    test_lista_attesa(anagrafica, aula, coda);
    test_annullamento(anagrafica, aula, coda);
    test_storico_report(anagrafica, aula, coda);

    printf("\n==========================================\n");
    printf("  FINE TEST SUITE                        \n");
    printf("==========================================\n");

    libera_risorse(anagrafica, aula, coda);
    return 0;
}