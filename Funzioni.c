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