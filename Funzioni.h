/* ==========================================================================
 * File:    funzioni.h
 * Modulo:  API pubblica del sistema Gestione Aula Studio
 * ---------------------------------------------------------------------------
 * Descrizione:
 *   Header pubblico che espone i PROTOTIPI delle funzioni implementate
 *   in funzioni.c. E' il "contratto" del modulo: chi vuole usare il
 *   sistema (main.c, test.c, eventuali estensioni future) include
 *   solo questo file.
 *
 *   I tipi delle strutture (TabellaHashStudenti, TurnoAula, CodaAttesa,
 *   Studente, ...) sono dichiarati come opachi in strutture.h e
 *   definiti privatamente in funzioni.c: il chiamante li manipola
 *   esclusivamente tramite le funzioni dichiarate qui (information
 *   hiding).
 *
 * Convenzione di documentazione:
 *   Per ogni funzione, quando rilevante, vengono indicati:
 *     - cosa fa (semantica) e PERCHE' nei casi non ovvi;
 *     - le Pre-condizioni che il chiamante deve garantire;
 *     - le Post-condizioni garantite al termine;
 *     - il valore di ritorno e gli eventuali effetti collaterali
 *       (scrittura su file, stampa su stdout, allocazioni).
 *
 *   I commenti su questo header descrivono il contratto pubblico.
 *   I dettagli implementativi sono invece in funzioni.c.
 *
 * Note:
 *   - L'inclusione di <time.h> serve a esporre il tipo time_t usato in
 *     aggiorna_orario_automatico.
 *   - Le guardie #ifndef/#define/#endif evitano la doppia inclusione.
 * ========================================================================== */
 
#ifndef FUNZIONI_H
#define FUNZIONI_H
 
#include "strutture.h"
#include <time.h>
 
/* ==========================================================================
 * INIZIALIZZAZIONE E GESTIONE MEMORIA
 * ========================================================================== */
 
/*
 * inizializza_sistema:
 *   Azzera tutte le strutture dati gia' allocate dal chiamante: bucket
 *   della tabella hash a NULL, coda vuota, posti LIBERO, contatori a zero,
 *   data reale di sistema e fascia di default MATTINA.
 *
 * Pre:  t, aula, coda non NULL (gia' allocate, possono contenere dati casuali).
 * Post: strutture in stato pulito e coerente.
 */
void inizializza_sistema(TabellaHashStudenti* t, TurnoAula* aula, CodaAttesa* coda);
 
/*
 * inizializza_sistema_dinamico:
 *   Variante "tutto-in-uno": alloca dinamicamente le tre strutture e poi
 *   le inizializza. Riceve DOPPI PUNTATORI perche' deve modificare le
 *   variabili-puntatore del chiamante (che inizialmente sono NULL).
 *
 *   E' la variante da usare dal main, dato che i tipi sono opachi: il
 *   chiamante non puo' fare malloc(sizeof(TabellaHashStudenti)) perche'
 *   non ne conosce la dimensione.
 *
 *   In caso di malloc fallita termina il programma con exit(1): senza
 *   queste strutture l'applicazione non puo' proseguire.
 *
 * Pre:  t, aula, coda sono indirizzi di variabili-puntatore valide.
 * Post: *t, *aula, *coda puntano a strutture allocate e inizializzate.
 */
void inizializza_sistema_dinamico(TabellaHashStudenti** t, TurnoAula** aula, CodaAttesa** coda);
 
/*
 * libera_risorse:
 *   Rilascia tutta la memoria allocata dinamicamente: nodi di ogni bucket,
 *   nodi della coda e le tre strutture principali. Da chiamare una sola
 *   volta in chiusura per evitare memory leak.
 *
 * Pre:  t, aula, coda possono essere NULL (caso difensivo).
 * Post: tutta la memoria delle strutture passate e' liberata; i puntatori
 *       del chiamante restano formalmente validi come valore ma non vanno
 *       piu' dereferenziati.
 */
void libera_risorse(TabellaHashStudenti* t, TurnoAula* aula, CodaAttesa* coda);
 
/*
 * svuota_coda:
 *   Libera tutti i nodi della coda lasciandola vuota ma ancora valida
 *   (head/tail = NULL, dimensione = 0). Usata principalmente nei reset
 *   tra test successivi.
 *
 *   Nota: per il cambio fascia normale NON si usa questa funzione, perche'
 *   la coda contiene anche prenotazioni anticipate per turni futuri che
 *   vanno preservate; la pulizia selettiva e' fatta da
 *   cambio_fascia_automatica.
 *
 * Pre:  coda puo' essere NULL.
 * Post: coda vuota e coerente.
 */
void svuota_coda(CodaAttesa* coda);
 
 
/* ==========================================================================
 * GETTER (Information Hiding)
 * ---------------------------------------------------------------------------
 * I tipi sono opachi: il chiamante non puo' fare aula->fascia o
 * coda->dimensione direttamente. Tutti i getter sono difensivi sui NULL e
 * restituiscono un valore "neutro" quando il puntatore non e' valido, in
 * modo che il chiamante non debba sparpagliare controlli ovunque.
 * ========================================================================== */
 
/*
 * get_fascia_aula:
 *   Restituisce la fascia oraria corrente dell'aula.
 *   Ritorna MATTINA se aula e' NULL.
 */
FasciaOraria get_fascia_aula(TurnoAula* aula);
 
/*
 * get_posti_occupati_totali:
 *   Restituisce il numero di posti impegnati (stato PRENOTATO + OCCUPATO).
 *   Ritorna 0 se aula e' NULL.
 */
int get_posti_occupati_totali(TurnoAula* aula);
 
/*
 * get_dimensione_coda:
 *   Restituisce il numero di studenti in lista d'attesa.
 *   Ritorna 0 se coda e' NULL.
 */
int get_dimensione_coda(CodaAttesa* coda);
 
/*
 * get_data_aula:
 *   Restituisce la stringa della data del turno (formato GG/MM/AAAA).
 *   Ritorna NULL se aula e' NULL.
 */
char* get_data_aula(TurnoAula* aula);
 
 
/* ==========================================================================
 * GESTIONE ANAGRAFICA (Tabella Hash)
 * ========================================================================== */
 
/*
 * calcola_hash:
 *   Calcola l'indice del bucket per una matricola usando l'algoritmo djb2
 *   (hash*33 + c, seed 5381).
 *
 * Pre:  matricola puo' essere NULL.
 * Post: nessuna modifica allo stato (funzione pura).
 *
 * Ritorna: intero in [0, BUCKETS-1]; 0 se matricola e' NULL.
 */
int calcola_hash(char* matricola);
 
/*
 * inserisci_studente:
 *   Inserisce uno Studente nella tabella hash con insert-in-head sul
 *   bucket (O(1)). Se la matricola esiste gia' rifiuta l'inserimento
 *   SENZA sovrascrivere i dati esistenti.
 *
 *   Nota: questa funzione richiede una variabile Studente, che il main
 *   non puo' dichiarare (tipo opaco). Per uso esterno preferire
 *   registra_studente.
 *
 * Pre:  t inizializzata.
 * Post: se nuovo, lo studente e' presente; in caso di duplicato o di
 *       malloc fallita la tabella e' invariata.
 */
void inserisci_studente(TabellaHashStudenti* t, Studente s);
 
/*
 * cerca_studente:
 *   Cerca uno studente per matricola scorrendo la lista del bucket.
 *
 * Pre:  t e matricola possono essere NULL (caso difensivo).
 * Post: nessuna modifica allo stato.
 *
 * Ritorna: puntatore allo Studente (vivo dentro la tabella) o NULL se
 *          non trovato. NON liberare il puntatore restituito.
 */
Studente* cerca_studente(TabellaHashStudenti* t, char* matricola);
 
/*
 * get_nome_studente:
 *   Getter del campo nome (Studente e' opaco).
 *
 * Pre:  s puo' essere NULL.
 * Post: nessuna modifica allo stato.
 *
 * Ritorna: puntatore al nome (sola lettura, vita legata al nodo nella
 *          tabella); stringa vuota "" se s e' NULL, in modo che il
 *          chiamante possa stampare il risultato senza controlli aggiuntivi.
 */
const char* get_nome_studente(Studente* s);
 
/*
 * registra_studente:
 *   API pubblica di registrazione studente, da usare dal main al posto
 *   di inserisci_studente (Studente e' opaco: il main non puo'
 *   costruirlo direttamente).
 *
 *   Costruisce internamente lo Studente, lo inserisce, e registra
 *   l'evento "REGISTRAZIONE" nello storico SOLO se l'inserimento e'
 *   avvenuto (non per i duplicati).
 *
 * Pre:  t inizializzata; matricola, nome, corso sono stringhe valide.
 * Post: studente inserito se nuovo; in ogni caso lo storico riflette
 *       solo gli inserimenti effettivi.
 */
void registra_studente(TabellaHashStudenti* t, char* matricola, char* nome, char* corso);
 
 
/* ==========================================================================
 * GESTIONE CODA D'ATTESA
 * ========================================================================== */
 
/*
 * accoda_studente:
 *   Inserisce uno studente in fondo alla coda con tail-insert (O(1)).
 *   La coda e' una FIFO, ma puo' contenere studenti per fasce diverse
 *   (prenotazioni anticipate): il campo fascia del nodo distingue i casi.
 *
 * Pre:  coda inizializzata; matricola e data stringhe valide.
 * Post: dimensione coda incrementata di 1; in caso di malloc fallita la
 *       coda resta invariata e l'errore e' segnalato su stderr.
 */
void accoda_studente(CodaAttesa* coda, char* matricola, char* data, FasciaOraria fascia);
 
/*
 * estrai_studente:
 *   Estrae (dequeue) il primo nodo della coda restituendolo scollegato.
 *
 *   IMPORTANTE: il chiamante e' responsabile della free() del nodo
 *   restituito. Questa funzione si limita a sganciarlo dalla lista,
 *   lasciando al caller il tempo di leggerne i campi.
 *
 * Pre:  coda puo' essere NULL.
 * Post: dimensione coda decrementata (se non vuota); head/tail coerenti.
 *
 * Ritorna: puntatore al nodo estratto, oppure NULL se la coda e' vuota/NULL.
 */
NodoAttesa* estrai_studente(CodaAttesa* coda);
 
 
/* ==========================================================================
 * LOGICA DI BUSINESS (Traccia 2)
 * ========================================================================== */
 
/*
 * effettua_prenotazione:
 *   Prenota un posto per uno studente in una fascia. La logica varia in
 *   base al rapporto tra fascia richiesta e fascia corrente:
 *     - fascia == corrente : assegna un posto LIBERO, oppure accoda se pieno;
 *     - fascia >  corrente : prenotazione anticipata, va sempre in coda;
 *     - fascia <  corrente : rifiutata (turno gia' concluso).
 *
 *   Previene anche prenotazioni di matricole non registrate e
 *   duplicati (sia su posto sia in coda).
 *
 * Pre:  sistema inizializzato; lo studente puo' essere o meno gia' registrato.
 * Post: posto assegnato, oppure studente accodato, oppure nessuna modifica
 *       in caso di errore o duplicato.
 */
void effettua_prenotazione(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda,
                           char* matricola, FasciaOraria fascia_scelta, OrarioVirtuale ora_attuale);
 
/*
 * effettua_checkin:
 *   Registra l'ingresso fisico in aula gestendo, in ordine di priorita':
 *     1. matricola non registrata -> errore;
 *     2. posto gia' assegnato -> conferma o avviso doppio check-in;
 *     3. promozione dalla coda (fascia corrente "matura");
 *     4. attesa per fascia futura -> rifiuto temporaneo;
 *     5. ingresso diretto (posti liberi E coda vuota);
 *     6. inserimento in coda altrimenti (priorita' FIFO).
 *
 * Pre:  sistema inizializzato.
 * Post: lo stato dell'aula/coda riflette lo scenario applicato; evento
 *       registrato nello storico.
 */
void effettua_checkin(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda,
                      char* matricola, OrarioVirtuale ora_attuale);
 
/*
 * effettua_checkout:
 *   Registra l'uscita di uno studente. Liberato il posto, vi fa subentrare
 *   il primo studente in coda con fascia uguale a quella corrente
 *   (stato PRENOTATO, in attesa del suo check-in). Se nessuno e'
 *   compatibile, il posto torna LIBERO.
 *
 * Pre:  sistema inizializzato.
 * Post: il posto torna LIBERO o passa al subentrante; storico aggiornato
 *       con CHECK-OUT e, se applicabile, SUBENTRO.
 */
void effettua_checkout(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda,
                       char* matricola, OrarioVirtuale ora_attuale);
 
/*
 * annulla_prenotazione:
 *   Annulla la prenotazione di uno studente. Scenari:
 *     A) posto fisico PRENOTATO -> liberato (con eventuale subentro
 *        dalla coda, se c'e' un compatibile per fascia);
 *     B) prenotazione anticipata in coda -> nodo rimosso e
 *        totale_prenotazioni decrementato.
 *
 *   Annulla solo lo stato PRENOTATO: chi e' gia' seduto (OCCUPATO) non
 *   "annulla", ma "esce" tramite effettua_checkout.
 *
 * Pre:  sistema inizializzato.
 * Post: vedi scenari sopra; in assenza di prenotazione viene segnalato
 *       un errore non bloccante.
 */
void annulla_prenotazione(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda,
                          char* matricola, OrarioVirtuale ora_attuale);
 
 
/* ==========================================================================
 * GESTIONE TEMPO E TURNI
 * ========================================================================== */
 
/*
 * aggiorna_orario_automatico:
 *   Avanza l'orario virtuale in base al tempo reale trascorso dall'ultima
 *   chiamata (1 secondo reale = 120 secondi virtuali) e innesca il cambio
 *   fascia automatico al superamento delle soglie orarie. L'avanzamento
 *   secondo-per-secondo e' robusto rispetto a eventuali "balzi"
 *   dell'orologio (es. dopo uno sleep lungo).
 *
 * Pre:  tutti i puntatori non NULL.
 * Post: *ora avanzato; *ultimo_aggiornamento aggiornato; eventuale
 *       cambio fascia eseguito.
 */
void aggiorna_orario_automatico(OrarioVirtuale* ora, time_t* ultimo_aggiornamento,
                                TurnoAula* aula, CodaAttesa* coda);
 
/*
 * cambio_fascia_automatica:
 *   Chiude il turno corrente e prepara il successivo:
 *     1. conta no-show (posti PRENOTATO a fine turno) e checkout forzati
 *        (posti OCCUPATO);
 *     2. pulizia SELETTIVA della coda: rimuove solo i nodi della fascia
 *        appena conclusa; le prenotazioni anticipate per turni futuri
 *        restano in coda;
 *     3. reset di posti e contatori del turno;
 *     4. promozione FIFO dei nodi della nuova fascia ai posti liberi
 *        (assegnati come PRENOTATO).
 *
 *   La pulizia selettiva e' il motivo per cui qui NON si usa svuota_coda.
 *
 * Pre:  aula e coda non NULL.
 * Post: aula azzerata e impostata sulla nuova fascia; coda epurata dei
 *       nodi della fascia conclusa; nodi compatibili promossi ai posti.
 */
void cambio_fascia_automatica(TurnoAula* aula, CodaAttesa* coda, FasciaOraria nuova_fascia);
 
/*
 * is_orario_valido:
 *   Verifica se un orario ricade nei limiti di una fascia:
 *     - MATTINA    : 09:00 - 13:00
 *     - POMERIGGIO : 14:00 - 18:00
 *     - SERA       : 18:00 - 22:00
 *
 *   Ritorna: 1 se valido, 0 altrimenti.
 */
int is_orario_valido(OrarioVirtuale adesso, FasciaOraria fascia);
 
/*
 * orario_in_secondi:
 *   Converte un OrarioVirtuale in secondi assoluti dalla mezzanotte.
 *   Utile per confronti numerici diretti tra fasce orarie.
 *
 *   Ritorna: secondi trascorsi dalla mezzanotte.
 */
long orario_in_secondi(OrarioVirtuale o);
 
 
/* ==========================================================================
 * REPORTISTICA E MONITORAGGIO
 * ========================================================================== */
 
/*
 * salva_storico_accesso:
 *   Aggiunge una riga al file "storico_accessi.txt" (apertura in append)
 *   con timestamp, dati anagrafici dello studente (o "N/D" se non
 *   reperibili) e tipo di operazione.
 *
 *   anagrafica puo' essere NULL: in tal caso si salta il lookup e si
 *   scrive "N/D" nei campi nome/corso. Cosi' si possono loggare anche
 *   eventi automatici (es. NO-SHOW a fine turno) quando il chiamante
 *   non ha la tabella sotto mano.
 *
 * Pre:  il file deve essere accessibile in scrittura nella directory
 *       di lavoro (altrimenti viene segnalato errore su stderr).
 * Post: una nuova riga aggiunta in fondo al file.
 */
void salva_storico_accesso(TabellaHashStudenti* anagrafica, char* matricola,
                           char* operazione, OrarioVirtuale ora);
 
/*
 * visualizza_storico_accessi:
 *   Legge tutto il file "storico_accessi.txt" e lo stampa a video.
 *   Se il file non esiste lo segnala in modo non bloccante.
 *
 * Pre:  nessuna.
 * Post: nessuna modifica allo stato; output su stdout.
 */
void visualizza_storico_accessi();
 
/*
 * genera_report_aula:
 *   Stampa il report completo dell'aula: contatori del turno corrente,
 *   snapshot istantaneo (presenti, prenotati, posti liberi, coda),
 *   occupazione cumulativa per fascia, barra grafica di saturazione e
 *   storico letto da file. Funzione di sola lettura.
 *
 * Pre:  aula e coda non NULL.
 * Post: nessuna modifica alle strutture; output su stdout.
 */
void genera_report_aula(TurnoAula* aula, CodaAttesa* coda);
 
/*
 * visualizza_situazione_corrente:
 *   Stampa una vista COMPATTA dello stato dell'aula (per posto e
 *   matricola) e della coda d'attesa. Versione "leggera" di
 *   visualizza_studenti_per_stato, senza nome e corso.
 *
 * Pre:  aula e coda non NULL.
 * Post: nessuna modifica alle strutture; output su stdout.
 */
void visualizza_situazione_corrente(TurnoAula* aula, CodaAttesa* coda);
 
/*
 * visualizza_studenti_per_stato:
 *   Stampa l'elenco NOMINATIVO degli studenti diviso in tre categorie:
 *   PRESENTI (OCCUPATO), PRENOTATI (PRENOTATO) e IN CODA. Per ognuno
 *   mostra matricola, nome e corso recuperati dall'anagrafica, con un
 *   placeholder se la matricola non e' presente nell'anagrafica
 *   (es. matricole fittizie usate dai test).
 *
 * Pre:  anagrafica, aula e coda non NULL.
 * Post: nessuna modifica alle strutture; output su stdout.
 */
void visualizza_studenti_per_stato(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda);
 
/*
 * esegui_test_completo:
 *   Esegue la batteria di test automatici integrata nell'applicazione
 *   principale, con output [PASS]/[FAIL] per ogni verifica. E' una
 *   versione "demo" interna; la suite di test vera e propria e' in
 *   test.c, compilata separatamente come eseguibile a se' stante.
 *
 *   Modifica pesantemente lo stato del sistema: pensata per uso
 *   diagnostico, non per esecuzione ordinaria.
 *
 * Pre:  sistema gia' inizializzato.
 * Post: strutture modificate dai test; file di storico popolato.
 */
void esegui_test_completo(TabellaHashStudenti* anagrafica, TurnoAula* aula, CodaAttesa* coda);
 
/*
 * mostra_menu:
 *   Stampa il menu testuale principale dell'applicazione. Non legge
 *   input: la lettura della scelta e' responsabilita' del main.
 *
 * Pre:  nessuna.
 * Post: output su stdout.
 */
void mostra_menu();
 
#endif /* FUNZIONI_H */
 