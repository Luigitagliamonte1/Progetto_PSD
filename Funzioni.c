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

/* ==========================================================================
 * GESTIONE PRENOTAZIONI E ACCESSI
 * ========================================================================== */

/*
 * effettua_prenotazione:
 *   Prenota un posto in aula per uno studente in una specifica fascia oraria.
 *
 *   La logica varia in base al rapporto tra fascia richiesta e fascia corrente:
 *     - fascia_scelta == fascia corrente:
 *           assegna un posto LIBERO se disponibile, altrimenti accoda.
 *     - fascia_scelta >  fascia corrente:
 *           prenotazione anticipata: lo studente va sempre in coda con la
 *           sua fascia futura. Il posto fisico gli verra' assegnato al
 *           cambio turno (cambio_fascia_automatica).
 *     - fascia_scelta <  fascia corrente:
 *           RIFIUTATA: non si puo' prenotare per un turno gia' concluso.
 *
 *   Vengono inoltre prevenuti:
 *     - prenotazioni di matricole non registrate in anagrafica;
 *     - doppie prenotazioni sullo stesso turno (sia in posto sia in coda).
 *
 * Parametri:
 *   anagrafica, aula, coda: strutture non NULL.
 *   matricola: stringa valida.
 *   fascia_scelta: la fascia per cui si vuole prenotare.
 *   ora_attuale: timestamp virtuale dell'operazione (per lo storico).
 *
 * Pre:  sistema inizializzato; lo studente puo' essere o meno gia' registrato
 *       (in caso negativo la funzione segnala errore e termina).
 * Post: posto assegnato OPPURE studente messo in coda OPPURE nessuna modifica
 *       (in caso di errore o duplicato).
 */
void effettua_prenotazione(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda, char* matricola, FasciaOraria fascia_scelta, OrarioVirtuale ora_attuale) {
    int i;

    /* 1. Verifica anagrafica: prenotare e' permesso solo agli studenti
     *    gia' registrati nel sistema. */
    if (cerca_studente(anagrafica, matricola) == NULL) {
        printf("[ERRORE] Matricola %s non registrata. Impossibile prenotare.\n", matricola);
        return;
    }

    /* 2. Rifiuto per fascia passata: non ha senso prenotare un turno
     *    gia' concluso. */
    if (fascia_scelta < aula->fascia) {
        printf("[ERRORE] La fascia %s e' gia' conclusa. Puoi prenotare solo per %s o turni successivi.\n",
               (fascia_scelta == MATTINA ? "MATTINA" : (fascia_scelta == POMERIGGIO ? "POMERIGGIO" : "SERA")),
               (aula->fascia == MATTINA ? "MATTINA" : (aula->fascia == POMERIGGIO ? "POMERIGGIO" : "SERA")));
        return;
    }

    /* 3. Anti-duplicato (posti): lo studente ha gia' un posto assegnato? */
    for (i = 0; i < MAX_POSTI; i++) {
        if (aula->posti[i].stato != LIBERO &&
            strcmp(aula->posti[i].matricola_studente, matricola) == 0) {
            printf("[AVVISO] Hai gia' una prenotazione al posto %d per il turno corrente.\n", i + 1);
            return;
        }
    }

    /* 4. Anti-duplicato (coda): lo studente e' gia' in attesa
     *    per la stessa fascia? Controllato in un blocco a parte per
     *    isolare la dichiarazione di curr. */
    {
        NodoAttesa* curr = coda->head;
        while (curr != NULL) {
            if (strcmp(curr->matricola, matricola) == 0 && curr->fascia == fascia_scelta) {
                printf("[AVVISO] Sei gia' in lista d'attesa per la fascia %s.\n",
                       (fascia_scelta == MATTINA ? "MATTINA" : (fascia_scelta == POMERIGGIO ? "POMERIGGIO" : "SERA")));
                return;
            }
            curr = curr->next;
        }
    }

    /* 5. Prenotazione anticipata: si vuole prenotare un turno futuro.
     *    Va sempre in coda (anche se l'aula corrente fosse vuota), perche'
     *    i posti del turno futuro non esistono ancora come entita' assegnabile. */
    if (fascia_scelta > aula->fascia) {
        printf("[PRENOTAZIONE ANTICIPATA] La fascia %s non e' ancora iniziata.\n"
               "  Verrai inserito in lista d'attesa e avrai priorita' all'apertura del turno.\n",
               (fascia_scelta == POMERIGGIO ? "POMERIGGIO" : "SERA"));
        accoda_studente(coda, matricola, aula->data, fascia_scelta);
        aula->totale_prenotazioni++;
        salva_storico_accesso(anagrafica, matricola,
                       (fascia_scelta == MATTINA ? "PRENOTAZIONE ANTICIPATA [MATTINA]"
                       : fascia_scelta == POMERIGGIO ? "PRENOTAZIONE ANTICIPATA [POMERIGGIO]"
                       : "PRENOTAZIONE ANTICIPATA [SERA]"), ora_attuale);
        return;
    }

    /* 6. Prenotazione per la fascia corrente: assegna posto se possibile,
     *    altrimenti accoda. */
    {
        int posti_liberi = 0;
        for (i = 0; i < MAX_POSTI; i++)
            if (aula->posti[i].stato == LIBERO) posti_liberi++;

        if (posti_liberi > 0) {
            /* Assegnazione first-fit: si prende il primo posto LIBERO trovato. */
            for (i = 0; i < MAX_POSTI; i++) {
                if (aula->posti[i].stato == LIBERO) {
                    aula->posti[i].stato = PRENOTATO;
                    strncpy(aula->posti[i].matricola_studente, matricola, 11);
                    aula->posti[i].matricola_studente[11] = '\0';
                    aula->posti[i].ora_prenotazione = ora_attuale;
                    aula->posti_occupati++;
                    aula->totale_prenotazioni++;

                    printf("[SUCCESSO] Posto %d prenotato alle %02d:%02d:%02d per la fascia %s.\n",
                           i + 1, ora_attuale.ora, ora_attuale.minuti, ora_attuale.secondi,
                           (fascia_scelta == MATTINA ? "MATTINA" : (fascia_scelta == POMERIGGIO ? "POMERIGGIO" : "SERA")));
                    salva_storico_accesso(anagrafica, matricola,
                       (fascia_scelta == MATTINA ? "PRENOTAZIONE [MATTINA]"
                       : fascia_scelta == POMERIGGIO ? "PRENOTAZIONE [POMERIGGIO]"
                       : "PRENOTAZIONE [SERA]"), ora_attuale);
                    return;
                }
            }
        } else {
            /* Aula piena: l'unica alternativa accettabile e' la coda. */
            printf("[AULA PIENA] Tutti i %d posti sono occupati. Ti inserisco in coda...\n", MAX_POSTI);
            accoda_studente(coda, matricola, aula->data, fascia_scelta);
            aula->totale_prenotazioni++;
            salva_storico_accesso(anagrafica, matricola,
                       (fascia_scelta == MATTINA ? "PRENOTAZIONE IN CODA [MATTINA]"
                       : fascia_scelta == POMERIGGIO ? "PRENOTAZIONE IN CODA [POMERIGGIO]"
                       : "PRENOTAZIONE IN CODA [SERA]"), ora_attuale);
        }
    }
}

/*
 * accoda_studente:
 *   Aggiunge uno studente in coda alla lista d'attesa (enqueue O(1)).
 *
 *   Implementazione tail-insert: grazie al puntatore coda->tail non serve
 *   scorrere tutta la lista per arrivare in fondo. Il puntatore head viene
 *   aggiornato solo se la coda era vuota.
 *
 * Parametri:
 *   coda: non NULL.
 *   matricola, data: stringhe valide (vengono copiate nel nodo).
 *   fascia: fascia oraria per cui si attende.
 *
 * Pre:  coda inizializzata.
 * Post: dimensione coda incrementata di 1; eventuale allocazione fallita
 *       segnalata su stderr (la coda resta invariata).
 */
void accoda_studente(CodaAttesa* coda, char* matricola, char* data, FasciaOraria fascia) {
    if (coda == NULL) return;

    NodoAttesa* nuovo = (NodoAttesa*)malloc(sizeof(NodoAttesa));
    if (nuovo == NULL) {
        fprintf(stderr, "[ERRORE] Allocazione fallita in accoda_studente\n");
        return;
    }

    /* Copia sicura delle stringhe nei campi del nodo (vedi crea_studente
     * per il motivo della terminazione esplicita). */
    strncpy(nuovo->matricola, matricola, 11);
    nuovo->matricola[11] = '\0';
    strncpy(nuovo->data, data, 10);
    nuovo->data[10] = '\0';

    nuovo->fascia = fascia;
    nuovo->next = NULL;

    /* Tail-insert: se la coda e' vuota il nuovo nodo e' anche la testa,
     * altrimenti viene appeso dopo l'attuale tail. */
    if (coda->head == NULL) {
        coda->head = nuovo;
    } else {
        coda->tail->next = nuovo;
    }
    coda->tail = nuovo;
    coda->dimensione++;

    printf("[CODA] Studente %s aggiunto alla lista d'attesa (Posizione: %d).\n",
            matricola, coda->dimensione);
}

/*
 * effettua_checkin:
 *   Registra l'ingresso fisico di uno studente in aula. La logica gestisce
 *   tutti gli scenari possibili in ordine di priorita':
 *
 *     1. Matricola non registrata -> errore.
 *     2. Lo studente ha gia' un posto PRENOTATO/OCCUPATO -> conferma o
 *        avviso di doppio check-in.
 *     3. Lo studente e' in coda per la fascia corrente (prenotazione
 *        anticipata "maturata") -> viene estratto dalla coda e gli si
 *        assegna un posto.
 *     4. Lo studente e' in coda per una fascia futura -> deve aspettare.
 *     5. Nessuna prenotazione e c'e' un posto libero E la coda e' vuota
 *        -> ingresso diretto.
 *     6. Altrimenti -> messo in coda per la fascia corrente.
 *
 *   Il controllo sull'ora esatta della fascia e' omesso volutamente:
 *   l'orario virtuale e' accelerato (1 sec reale = 120 sec virtuali) e
 *   imporre una validazione stretta produrrebbe falsi negativi. La
 *   responsabilita' di tenere aperta/chiusa l'aula resta al meccanismo
 *   di cambio fascia automatico.
 *
 * Parametri:
 *   anagrafica, aula, coda: non NULL.
 *   matricola: stringa valida.
 *   ora_attuale: timestamp virtuale per lo storico.
 *
 * Pre:  sistema inizializzato.
 * Post: lo stato dell'aula/coda riflette uno degli scenari sopra elencati;
 *       l'evento (di qualsiasi tipo) e' registrato nello storico.
 */
void effettua_checkin(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda, char* matricola, OrarioVirtuale ora_attuale) {

    /* 1. Verifica anagrafica */
    if (cerca_studente(anagrafica, matricola) == NULL) {
        printf("[ERRORE] Matricola %s non presente in anagrafica. Registrati prima (Opz. 1).\n", matricola);
        return;
    }

    /* 2. Ricerca posto fisico gia' assegnato (PRENOTATO o OCCUPATO) */
    {
        int i;
        for (i = 0; i < MAX_POSTI; i++) {
            if (aula->posti[i].stato != LIBERO &&
                strcmp(aula->posti[i].matricola_studente, matricola) == 0) {

                if (aula->posti[i].stato == OCCUPATO) {
                    /* Doppio check-in: lo studente risulta gia' dentro. */
                    printf("[AVVISO] Risulti gia' seduto al posto %d.\n", i + 1);
                    return;
                }
                if (aula->posti[i].stato == PRENOTATO) {
                    /* Caso normale: la prenotazione si materializza in presenza. */
                    aula->posti[i].stato = OCCUPATO;
                    aula->totale_checkin++;
                    aula->accessi_per_fascia[(int)aula->fascia]++;
                    printf("[CHECK-IN] Prenotazione confermata. Benvenuto al posto %d!\n", i + 1);
                    salva_storico_accesso(anagrafica, matricola,
                               (aula->fascia == MATTINA ? "CHECK-IN CON PRENOTAZIONE [MATTINA]"
                               : aula->fascia == POMERIGGIO ? "CHECK-IN CON PRENOTAZIONE [POMERIGGIO]"
                               : "CHECK-IN CON PRENOTAZIONE [SERA]"), ora_attuale);
                    return;
                }
            }
        }
    }

    /* 3. Ricerca in coda per la fascia corrente.
     *    Caso particolare: lo studente aveva prenotato in anticipo per la
     *    fascia che ora e' diventata corrente, ma non e' stato ancora
     *    promosso ad un posto fisico (es. promosso da cambio_fascia ma
     *    aula piena, o ancora in coda perche' i posti erano gia' tutti
     *    presi). Lo estraiamo e gli diamo un posto come da prenotazione. */
    {
        NodoAttesa* curr = coda->head;
        NodoAttesa* prev = NULL;
        while (curr != NULL) {
            if (strcmp(curr->matricola, matricola) == 0 && curr->fascia == aula->fascia) {
                /* Scollegamento del nodo: gestiamo i tre casi (testa, mezzo, coda). */
                if (prev == NULL) coda->head = curr->next;
                else              prev->next  = curr->next;
                if (coda->tail == curr) coda->tail = prev;
                coda->dimensione--;
                free(curr);

                /* Assegnazione first-fit del primo posto LIBERO. */
                {
                    int i;
                    for (i = 0; i < MAX_POSTI; i++) {
                        if (aula->posti[i].stato == LIBERO) {
                            aula->posti[i].stato = OCCUPATO;
                            strncpy(aula->posti[i].matricola_studente, matricola, 11);
                            aula->posti[i].matricola_studente[11] = '\0';
                            aula->posti[i].ora_prenotazione = ora_attuale;
                            aula->posti_occupati++;
                            aula->totale_checkin++;
                            aula->accessi_per_fascia[(int)aula->fascia]++;
                            printf("[CHECK-IN] Prenotazione anticipata confermata. Benvenuto al posto %d!\n", i + 1);
                            salva_storico_accesso(anagrafica, matricola,
                                       (aula->fascia == MATTINA ? "CHECK-IN DA PRENOTAZIONE ANTICIPATA [MATTINA]"
                                       : aula->fascia == POMERIGGIO ? "CHECK-IN DA PRENOTAZIONE ANTICIPATA [POMERIGGIO]"
                                       : "CHECK-IN DA PRENOTAZIONE ANTICIPATA [SERA]"), ora_attuale);
                            return;
                        }
                    }
                    /* Estratto dalla coda ma nessun posto libero: rimettiamo
                     * in coda per non perdere lo studente. */
                    accoda_studente(coda, matricola, aula->data, aula->fascia);
                    printf("[AULA PIENA] Prenotazione anticipata trovata ma nessun posto libero. Rimesso in coda.\n");
                    return;
                }
            }
            prev = curr;
            curr = curr->next;
        }
    }

    /* 4. Controllo coda con fascia futura: lo studente ha una prenotazione
     *    anticipata che non e' ancora "maturata". Non puo' entrare ora. */
    {
        NodoAttesa* curr = coda->head;
        while (curr != NULL) {
            if (strcmp(curr->matricola, matricola) == 0 && curr->fascia > aula->fascia) {
                printf("[ATTESA] Hai una prenotazione per la fascia %s, ma siamo ancora in fascia %s.\n"
                       "         Attendi il cambio turno automatico per effettuare il check-in.\n",
                       (curr->fascia == POMERIGGIO ? "POMERIGGIO" : "SERA"),
                       (aula->fascia == MATTINA ? "MATTINA" : "POMERIGGIO"));
                return;
            }
            curr = curr->next;
        }
    }

    /* 5. Ingresso senza prenotazione.
     *    Concesso solo se: c'e' almeno un posto LIBERO E la coda e' vuota.
     *    Il secondo vincolo e' una scelta di equita': se qualcuno gia'
     *    aspetta in coda, non e' giusto che un nuovo arrivato gli "salti"
     *    davanti. Si conta posti_liberi direttamente invece di usare
     *    (MAX_POSTI - posti_occupati) perche' posti_occupati include
     *    anche i PRENOTATI non ancora arrivati. */
    {
        int posti_liberi = 0;
        int i;
        for (i = 0; i < MAX_POSTI; i++)
            if (aula->posti[i].stato == LIBERO) posti_liberi++;

        if (posti_liberi > 0 && coda->dimensione == 0) {
            for (i = 0; i < MAX_POSTI; i++) {
                if (aula->posti[i].stato == LIBERO) {
                    aula->posti[i].stato = OCCUPATO;
                    strncpy(aula->posti[i].matricola_studente, matricola, 11);
                    aula->posti[i].matricola_studente[11] = '\0';
                    aula->posti_occupati++;
                    aula->totale_checkin++;
                    aula->accessi_per_fascia[(int)aula->fascia]++;

                    printf("[SUCCESSO] Nessuna prenotazione. Posto libero %d assegnato direttamente.\n", i + 1);
                    salva_storico_accesso(anagrafica, matricola,
                                    (aula->fascia == MATTINA ? "CHECK-IN SENZA PRENOTAZIONE [MATTINA]"
                                    : aula->fascia == POMERIGGIO ? "CHECK-IN SENZA PRENOTAZIONE [POMERIGGIO]"
                                    : "CHECK-IN SENZA PRENOTAZIONE [SERA]"), ora_attuale);
                    return;
                }
            }
        } else {
            /* 6. Inserimento in lista d'attesa: aula piena oppure c'e' gia'
             *    qualcuno in coda con priorita' maggiore. */
            if (coda->dimensione > 0 && aula->posti_occupati < MAX_POSTI) {
                printf("[INFO] Ci sono persone in attesa prima di te. Ti aggiungo alla coda.\n");
            } else {
                printf("[AULA PIENA] Nessun posto disponibile.\n");
            }

            printf("Inserimento nella lista d'attesa per la fascia %s.\n",
                    (aula->fascia == MATTINA ? "MATTINA" : (aula->fascia == POMERIGGIO ? "POMERIGGIO" : "SERA")));

            accoda_studente(coda, matricola, aula->data, aula->fascia);
            salva_storico_accesso(anagrafica, matricola,
                    (aula->fascia == MATTINA ? "INSERIMENTO IN LISTA ATTESA [MATTINA]"
                    : aula->fascia == POMERIGGIO ? "INSERIMENTO IN LISTA ATTESA [POMERIGGIO]"
                    : "INSERIMENTO IN LISTA ATTESA [SERA]"), ora_attuale);
        }
    }
}

/*
 * estrai_studente:
 *   Estrae (dequeue) il primo nodo della coda d'attesa, restituendo
 *   un puntatore al nodo scollegato dalla lista.
 *
 *   Il chiamante e' responsabile della free() del nodo restituito:
 *   questa funzione si limita a scollegarlo, lasciando al caller il
 *   compito di leggerne i campi prima di rilasciare la memoria.
 *
 * Parametri:
 *   coda: puo' essere NULL (caso difensivo).
 *
 * Ritorna:
 *   Puntatore al nodo estratto, oppure NULL se la coda e' vuota/NULL.
 *
 * Pre:  -
 * Post: dimensione coda decrementata di 1 (se c'era qualcosa); head e tail
 *       aggiornati in modo coerente; il nodo restituito ha next == NULL.
 */
NodoAttesa* estrai_studente(CodaAttesa* coda) {
    if (coda == NULL || coda->head == NULL) {
        return NULL;
    }

    NodoAttesa* estratto = coda->head;
    coda->head = estratto->next;

    /* Se la coda e' diventata vuota anche tail va azzerato, altrimenti
     * resterebbe come dangling pointer al nodo appena estratto. */
    if (coda->head == NULL) {
        coda->tail = NULL;
    }

    /* Sgancio fisico: il nodo restituito non deve piu' essere collegato
     * alla catena, cosi' il caller puo' liberarlo in sicurezza. */
    estratto->next = NULL;

    /* Difesa contro inconsistenze (non dovrebbe mai succedere se la coda
     * e' stata sempre manipolata tramite le funzioni dedicate). */
    if (coda->dimensione > 0) {
        coda->dimensione--;
    }

    return estratto;
}