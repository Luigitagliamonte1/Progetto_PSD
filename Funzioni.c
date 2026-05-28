/* ==========================================================================
 * File:    funzioni.c
 * Modulo:  Gestione Aula Studio
 * ---------------------------------------------------------------------------
 * Descrizione:
 *   Implementazione delle funzioni di gestione di un'aula studio
 *   universitaria. Il sistema mantiene:
 *     - un'anagrafica degli studenti (tabella hash con chaining);
 *     - lo stato dei posti dell'aula per la fascia oraria corrente;
 *     - una coda FIFO d'attesa per chi non trova posto o prenota in anticipo;
 *     - uno storico persistente delle operazioni su file di testo.
 *
 *   Il file espone tipi opachi (Studente, TurnoAula, ecc.) i cui campi
 *   sono dichiarati qui e non nell'header: il chiamante (main) puo' usare
 *   solo i puntatori e le funzioni dell'API pubblica (information hiding).
 *
 * Convenzioni adottate:
 *   - Le funzioni pubbliche convalidano i puntatori in ingresso.
 *   - Gli errori non bloccanti sono segnalati con printf "[ERRORE]"/"[AVVISO]".
 *   - Gli errori critici (es. malloc fallita) usano fprintf su stderr.
 *   - Le stringhe di output evitano accenti per compatibilita' con
 *     console che non supportano UTF-8 (da qui "gia'", "e'", ecc.).
 * ========================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "strutture.h"

/* ==========================================================================
 * FORWARD DECLARATIONS
 * ---------------------------------------------------------------------------
 * Necessarie perche' alcune funzioni si chiamano a vicenda prima di essere
 * definite nel file (es. registra_studente -> cerca_studente,
 * effettua_prenotazione -> accoda_studente, ecc.). Tenerle tutte raggruppate
 * qui evita errori di compilazione dipendenti dall'ordine delle definizioni.
 * ========================================================================== */
int calcola_hash(char* matricola);
Studente* cerca_studente(TabellaHashStudenti* t, char* matricola);
void accoda_studente(CodaAttesa* coda, char* matricola, char* data, FasciaOraria fascia);
NodoAttesa* estrai_studente(CodaAttesa* coda);
void salva_storico_accesso(TabellaHashStudenti* anagrafica, char* matricola, char* operazione, OrarioVirtuale ora);
void visualizza_storico_accessi();
int is_orario_valido(OrarioVirtuale adesso, FasciaOraria fascia);
void svuota_coda(CodaAttesa* coda);
void cambio_fascia_automatica(TurnoAula* aula, CodaAttesa* coda, FasciaOraria nuova_fascia);
void annulla_prenotazione(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda, char* matricola, OrarioVirtuale ora_attuale);
void inizializza_sistema(TabellaHashStudenti* t, TurnoAula* aula, CodaAttesa* coda);

/* ==========================================================================
 * DEFINIZIONE DEI TIPI OPACHI
 * ---------------------------------------------------------------------------
 * I tipi sono dichiarati come "typedef struct X X;" nell'header pubblico
 * (strutture.h) e definiti qui. Questo nasconde i campi al chiamante e
 * forza l'uso dell'API (getter/setter, funzioni dedicate).
 * ========================================================================== */

/* Studente:
 * Dati anagrafici di un iscritto. La matricola e' la chiave univoca usata
 * sia come identificatore sia come input dell'hash. */
struct Studente {
    char matricola[12];        /* 11 caratteri + terminatore */
    char nome[60];             /* nome e cognome insieme     */
    char corso_di_laurea[60];
};

/* NodoStudente:
 * Elemento della lista concatenata usata in ogni bucket della tabella hash.
 * La lista risolve le collisioni (chaining): piu' studenti che cadono nello
 * stesso bucket vengono accodati qui invece di forzare un rehash. */
struct NodoStudente {
    Studente dati;
    struct NodoStudente *next;
};

/* TabellaHashStudenti:
 * Array di puntatori a liste di NodoStudente. La dimensione e' BUCKETS
 * (vedi strutture.h). La scelta di una hash table invece di un array lineare
 * permette ricerca/inserimento medi in O(1) anche con molte matricole. */
struct TabellaHashStudenti {
    NodoStudente *tabella[BUCKETS];
};

/* Posto:
 * Stato di un singolo posto dell'aula in un dato istante.
 * matricola_studente e' valido solo se stato != LIBERO. */
struct Posto {
    int numero_posto;
    StatoPosto stato;
    char matricola_studente[12];
    OrarioVirtuale ora_prenotazione;
};

/* TurnoAula:
 * Stato complessivo dell'aula per il turno (fascia) corrente.
 * Contiene sia lo stato istantaneo (posti, posti_occupati) sia i contatori
 * statistici usati dal report di fine turno.
 *
 * Nota: i contatori "totale_*" si riferiscono al turno corrente e vengono
 * azzerati a ogni cambio fascia in cambio_fascia_automatica(). L'array
 * accessi_per_fascia[] e' invece cumulativo per fascia, per il report. */
struct TurnoAula {
    char data[11];           /* Data del turno in formato GG/MM/AAAA              */
    FasciaOraria fascia;     /* Fascia oraria corrente (MATTINA/POMERIGGIO/SERA)  */
    Posto posti[MAX_POSTI];  /* Array dei posti fisici dell'aula                  */
    int posti_occupati;      /* Numero posti attualmente impegnati                */

    /* Contatori statistici del turno corrente (azzerati al cambio fascia) */
    int totale_prenotazioni;     /* Prenotazioni effettuate nel turno corrente   */
    int totale_checkin;          /* Check-in effettivi nel turno corrente        */
    int totale_checkout;         /* Uscite registrate nel turno corrente         */
    int totale_no_show;          /* Prenotati non presentati a fine turno        */
    int totale_espulsi_da_coda;  /* Studenti rimasti in coda al cambio fascia    */

    /* Contatori cumulativi per fascia (sopravvivono al cambio turno) */
    int accessi_per_fascia[3];   /* [0]=MATTINA  [1]=POMERIGGIO  [2]=SERA        */
};

/* NodoAttesa:
 * Elemento della coda FIFO. Memorizza anche la fascia richiesta perche'
 * la coda contiene studenti per turni differenti (prenotazioni anticipate). */
struct NodoAttesa {
    char matricola[12];
    char data[11];
    FasciaOraria fascia;
    struct NodoAttesa *next;
};

/* CodaAttesa:
 * Coda FIFO con puntatori a testa e coda, per consentire enqueue O(1)
 * (insert in tail) e dequeue O(1) (remove from head). Il campo dimensione
 * evita di dover riscorrere la lista per conoscerne la lunghezza. */
struct CodaAttesa {
    NodoAttesa *head;
    NodoAttesa *tail;
    int dimensione;
};

/* ==========================================================================
 * FUNZIONI DI BASSO LIVELLO: tabella hash
 * ========================================================================== */

/*
 * calcola_hash:
 *   Calcola l'indice di bucket per una matricola usando l'algoritmo
 *   djb2 di Dan Bernstein (hash * 33 + c, con seed 5381). Scelto perche'
 *   ha buona distribuzione su stringhe corte come le matricole e si
 *   implementa in poche righe senza dipendenze.
 *
 * Parametri:
 *   matricola: stringa C terminata da '\0'; puo' essere NULL.
 *
 * Ritorna:
 *   Un intero in [0, BUCKETS-1], oppure 0 se matricola e' NULL
 *   (scelta difensiva: evita crash, il caller poi rileva l'errore
 *   in cerca/inserisci).
 */
int calcola_hash(char* matricola) {
    if (matricola == NULL) return 0;

    unsigned long hash = 5381;
    int c;
    unsigned char* p = (unsigned char*)matricola;

    /* hash * 33 + c, implementato come ((hash << 5) + hash) + c
     * per evitare la moltiplicazione esplicita (micro-ottimizzazione
     * tipica della formulazione classica di djb2). */
    while ((c = *p++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return (int)(hash % BUCKETS);
}

/*
 * inserisci_studente:
 *   Inserisce uno Studente nella tabella hash. Se la matricola esiste gia',
 *   l'inserimento viene rifiutato (nessun aggiornamento dei dati esistenti).
 *
 *   L'inserimento avviene in testa al bucket: O(1) e non richiede di
 *   scorrere la lista del bucket, dato che il duplicato e' gia' stato
 *   escluso dal controllo iniziale.
 *
 * Parametri:
 *   t: tabella hash (non NULL).
 *   s: studente da inserire (passato per valore: la struct viene copiata
 *      nel nodo, quindi il chiamante non deve preoccuparsi del ciclo di vita).
 *
 * Pre:  t inizializzata (vedi inizializza_sistema).
 * Post: in caso di successo, lo studente e' presente nella tabella;
 *       in caso di duplicato o malloc fallita, la tabella e' invariata.
 */
void inserisci_studente(TabellaHashStudenti* t, Studente s) {
    if (cerca_studente(t, s.matricola) != NULL) {
        printf("[AVVISO] Studente %s gia' registrato.\n", s.matricola);
        return;
    }

    NodoStudente* nuovo_nodo_temp = (NodoStudente*)malloc(sizeof(NodoStudente));
    if (nuovo_nodo_temp == NULL) {
        fprintf(stderr, "[ERRORE CRITICO] Memoria esaurita. Impossibile registrare lo studente.\n");
        return;
    }

    nuovo_nodo_temp->dati = s;
    int indice = calcola_hash(s.matricola);

    /* Insert-in-head: il nuovo nodo diventa la testa del bucket. */
    nuovo_nodo_temp->next = t->tabella[indice];
    t->tabella[indice] = nuovo_nodo_temp;

    printf("[REGISTRAZIONE] Studente %s inserito con successo.\n", s.matricola);
}

/*
 * crea_studente:
 *   Costruisce un oggetto Studente popolando i suoi campi.
 *   Funzione di servizio per uso interno al modulo: serve a centralizzare
 *   la copia sicura delle stringhe (strncpy + terminazione esplicita).
 *
 * Parametri:
 *   matricola, nome, corso: stringhe valide (non NULL).
 *
 * Pre:  i tre parametri stringa sono non NULL e terminati.
 * Post: ritorna uno Studente con i campi copiati e sicuramente terminati
 *       da '\0' anche se le stringhe sorgenti eccedevano la capienza.
 */
Studente crea_studente(char* matricola, char* nome, char* corso) {
    Studente s;
    /* Limitiamo la copia a (dimensione_campo - 1) e forziamo il '\0' finale:
     * strncpy da sola NON garantisce la terminazione se la sorgente e'
     * piu' lunga del limite. */
    strncpy(s.matricola, matricola, 11); s.matricola[11] = '\0';
    strncpy(s.nome, nome, 59);           s.nome[59]       = '\0';
    strncpy(s.corso_di_laurea, corso, 59); s.corso_di_laurea[59] = '\0';
    return s;
}

/*
 * get_nome_studente:
 *   Getter del campo nome di uno Studente. Necessario perche' Studente
 *   e' un tipo opaco: il main non puo' accedere a s->nome direttamente.
 *
 * Parametri:
 *   s: puntatore a Studente; puo' essere NULL.
 *
 * Ritorna:
 *   Puntatore a stringa (sola lettura, vita legata al nodo nella hash).
 *   Stringa vuota se s e' NULL: cosi' il caller puo' fare printf senza
 *   controlli aggiuntivi e senza rischio di dereferenziare un NULL.
 */
const char* get_nome_studente(Studente* s) {
    if (s == NULL) return "";
    return s->nome;
}

/*
 * registra_studente:
 *   API pubblica per registrare uno studente partendo dai tre campi
 *   stringa. E' l'unico modo che ha il main di inserire studenti, dato
 *   che Studente e' opaco: il main non puo' dichiarare variabili Studente
 *   ne' accedere ai suoi campi direttamente.
 *
 *   Rispetto a inserisci_studente, in piu':
 *     - costruisce internamente lo Studente con crea_studente;
 *     - registra l'evento "REGISTRAZIONE" nello storico, ma solo se
 *       lo studente e' stato effettivamente inserito (non per i duplicati).
 *
 * Parametri:
 *   t, matricola, nome, corso: tutti non NULL.
 *
 * Pre:  t inizializzata.
 * Post: studente inserito (se nuovo) e storico aggiornato di conseguenza.
 */
void registra_studente(TabellaHashStudenti* t, char* matricola, char* nome, char* corso) {
    /* Salviamo lo stato "prima" per distinguere nuova registrazione da duplicato:
     * inserisci_studente non ritorna un esito, quindi controlliamo noi. */
    int era_presente = (cerca_studente(t, matricola) != NULL);
    inserisci_studente(t, crea_studente(matricola, nome, corso));

    /* Logghiamo solo se l'inserimento e' effettivamente avvenuto. La seconda
     * verifica con cerca_studente copre anche il caso in cui la malloc sia
     * fallita: in quel caso non vogliamo scrivere una falsa registrazione. */
    if (!era_presente && cerca_studente(t, matricola) != NULL) {
        /* Ora 00:00:00 perche' la registrazione e' un evento amministrativo
         * non legato all'orario virtuale del turno. */
        OrarioVirtuale ora_zero = {0, 0, 0};
        salva_storico_accesso(t, matricola, "REGISTRAZIONE", ora_zero);
    }
}
