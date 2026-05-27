/* ==========================================================================
 * File:    strutture.h
 * Modulo:  Tipi pubblici del sistema Gestione Aula Studio
 * ---------------------------------------------------------------------------
 * Descrizione:
 *   Header pubblico che espone le COSTANTI di configurazione, i TIPI
 *   ENUMERATIVI e le DICHIARAZIONI FORWARD (tipi opachi) usati dal modulo.
 *
 *   Le definizioni complete delle struct (campi interni) sono volutamente
 *   nascoste in funzioni.c: qui sono dichiarate solo con "typedef struct X X;"
 *   in modo che il chiamante possa usarle SOLO tramite puntatori e
 *   tramite le funzioni dell'API (information hiding).
 *
 *   Conseguenze pratiche dell'information hiding:
 *     - il chiamante NON puo' dichiarare variabili dei tipi opachi
 *       sullo stack (es. "Studente s;" non compila qui);
 *     - il chiamante NON puo' accedere ai campi (es. "aula->fascia"
 *       fuori da funzioni.c non compila);
 *     - tutta la creazione/distruzione passa per le funzioni dedicate
 *       (registra_studente, inizializza_sistema_dinamico, libera_risorse).
 *
 * Note:
 *   Le guardie di inclusione (#ifndef/#define/#endif) evitano la doppia
 *   inclusione del file in una singola unita' di compilazione, che
 *   produrrebbe errori di "redefinition" sui typedef.
 * ========================================================================== */

#ifndef STRUTTURE_H
#define STRUTTURE_H

/* ==========================================================================
 * COSTANTI DI CONFIGURAZIONE
 * ========================================================================== */

/* BUCKETS:
 *   Numero di bucket della tabella hash dell'anagrafica. Scelto come
 *   numero primo per ridurre la probabilita' di collisioni con l'algoritmo
 *   djb2 usato in calcola_hash (i numeri primi distribuiscono meglio i
 *   resti modulo). Va dimensionato in funzione del numero atteso di
 *   studenti: con 101 bucket si gestiscono comodamente centinaia di
 *   matricole mantenendo le liste di chaining corte. */
#define BUCKETS 101

/* MAX_POSTI:
 *   Capienza massima dell'aula, cioe' la dimensione fissa dell'array
 *   di posti dentro TurnoAula. Modificando questo valore cambia la
 *   capacita' dell'aula senza dover toccare il resto del codice. */
#define MAX_POSTI 100

/* ==========================================================================
 * TIPI ENUMERATIVI
 * ========================================================================== */

/* FasciaOraria:
 *   Identifica la fascia oraria di un turno. L'ORDINE dei valori e'
 *   significativo: il codice usa confronti diretti (<, >, ==) tra
 *   fasce per distinguere prenotazioni passate, correnti e future
 *   (vedi effettua_prenotazione). Quindi MATTINA < POMERIGGIO < SERA
 *   NON va alterato senza aggiornare la logica di confronto. */
typedef enum { MATTINA, POMERIGGIO, SERA } FasciaOraria;

/* StatoPosto:
 *   Stato di un singolo posto dell'aula.
 *     LIBERO    -> nessuno assegnato, pronto per essere occupato;
 *     PRENOTATO -> assegnato a uno studente che non e' ancora arrivato
 *                  (post-prenotazione o post-subentro dalla coda);
 *     OCCUPATO  -> studente fisicamente presente (post-check-in).
 *   Il flusso normale e': LIBERO -> PRENOTATO -> OCCUPATO -> LIBERO. */
typedef enum { LIBERO, PRENOTATO, OCCUPATO } StatoPosto;

/* ==========================================================================
 * TIPI DI VALORE (non opachi)
 * ========================================================================== */

/* OrarioVirtuale:
 *   Rappresenta un istante "virtuale" della giornata (ora simulata
 *   dell'aula, non l'ora di sistema). E' un tipo trasparente perche'
 *   viene passato per valore in molte API (es. salva_storico_accesso):
 *   tenerlo pubblico evita allocazioni inutili e getter superflui.
 *   L'avanzamento e' gestito da aggiorna_orario_automatico. */
typedef struct {
    int ora, minuti, secondi;
} OrarioVirtuale;
