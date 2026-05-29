/* ==========================================================================
 * File:    main.c
 * Modulo:  Punto di ingresso del sistema Gestione Aula Studio
 * ---------------------------------------------------------------------------
 * Descrizione:
 *   Entry point del programma. Si occupa di:
 *     - inizializzare le strutture dinamiche del sistema (anagrafica
 *       studenti, stato dell'aula, coda d'attesa);
 *     - mantenere l'orario virtuale aggiornato a ogni iterazione del
 *       menu (la logica vera e' in aggiorna_orario_automatico);
 *     - presentare il menu testuale e instradare la scelta dell'utente
 *       verso la funzione corretta di funzioni.c.
 *
 *   Il main NON conosce i campi interni delle strutture: usa solo
 *   puntatori opachi e l'API pubblica esposta da funzioni.h
 *   (information hiding).
 *
 * Convenzioni adottate:
 *   - Letture da stdin con scanf + getchar() per "consumare" il
 *     newline residuo, oppure fgets + strcspn per cancellare '\n'.
 *   - Su input numerico non valido si svuota il buffer e si ripresenta
 *     il menu invece di terminare il programma.
 *   - La liberazione della memoria avviene in libera_risorse, chiamata
 *     esclusivamente nel ramo di uscita (case 0).
 * ========================================================================== */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "funzioni.h"

/*
 * main:
 *   Ciclo principale dell'applicazione. Inizializza il sistema, poi
 *   entra in un loop do-while che a ogni iterazione:
 *     1. aggiorna l'orario virtuale (e' qui che si gestisce il cambio
 *        fascia automatico, vedi aggiorna_orario_automatico);
 *     2. stampa lo stato corrente (ora e turno);
 *     3. mostra il menu e legge la scelta;
 *     4. esegue l'operazione corrispondente.
 *
 *   Il loop termina solo quando l'utente sceglie 0 (uscita): in quel
 *   ramo viene chiamata libera_risorse per evitare memory leak.
 *
 * Ritorna:
 *   0 in caso di chiusura normale. Non sono previsti exit code di errore
 *   a questo livello: gli errori applicativi vengono segnalati a video
 *   e non interrompono il programma.
 */
int main()
{
    /* Strutture principali dichiarate come PUNTATORI inizializzati a NULL.
     * Sono tipi opachi: il main non puo' conoscere ne' la loro dimensione
     * ne' i loro campi interni. La memoria viene allocata e i campi
     * inizializzati dentro inizializza_sistema_dinamico, che riceve gli
     * INDIRIZZI di questi puntatori (doppio puntatore) proprio per poterli
     * modificare. */
    TabellaHashStudenti* anagrafica = NULL;
    TurnoAula* aula = NULL;
    CodaAttesa* coda = NULL;

    /* Orario virtuale di partenza: 09:00:00, ovvero l'inizio della fascia
     * MATTINA. Sara' poi avanzato automaticamente da
     * aggiorna_orario_automatico in proporzione al tempo reale trascorso
     * (1 secondo reale = 120 secondi virtuali). */
    OrarioVirtuale ora_attuale = {9, 0, 0};

    /* Timestamp reale dell'ultima volta in cui abbiamo aggiornato l'orario
     * virtuale. Serve a calcolare il delta da convertire in tempo virtuale.
     * Lo inizializziamo a "ora" cosi' il primo aggiornamento non produrra'
     * un balzo enorme al primo giro di loop. */
    time_t ultimo_controllo = time(NULL);

    inizializza_sistema_dinamico(&anagrafica, &aula, &coda);

    /* Variabili riusate dai vari case del menu. Dichiarate qui (e non
     * dentro i singoli case) per evitare scope ambigui dovuti alla
     * mancanza di parentesi graffe esplicite in alcuni rami. */
    int scelta;
    char mat[20], nome[60], corso[60];

    do
    {
        /* Avanziamo l'orario virtuale PRIMA di mostrare il menu, cosi'
         * l'utente vede sempre uno stato aggiornato. E' anche qui che
         * puo' scattare il cambio fascia automatico. */
        aggiorna_orario_automatico(&ora_attuale, &ultimo_controllo, aula, coda);

        printf("\n--- STATO SISTEMA ---");
        printf("\nOra: %02d:%02d:%02d | Turno: %s",
                ora_attuale.ora, ora_attuale.minuti, ora_attuale.secondi,
                (get_fascia_aula(aula) == MATTINA ? "MATTINA" :
                (get_fascia_aula(aula) == POMERIGGIO ? "POMERIGGIO" : "SERA")));

        mostra_menu();

        /* Lettura difensiva: scanf ritorna il numero di item letti.
         * Se l'utente digita testo invece di un numero, il buffer
         * resta "sporco" e va svuotato manualmente, altrimenti il
         * loop andrebbe in tilt rileggendo gli stessi caratteri
         * a ogni iterazione. */
        if (scanf("%d", &scelta) != 1) {
            printf("Inserire un numero valido.\n");
            while(getchar() != '\n'); /* consuma tutta la riga residua */
            continue;
        }
        getchar(); /* consuma il '\n' lasciato da scanf */

        switch(scelta)
        {
        case 1: /* REGISTRAZIONE STUDENTE */
        {
            /* scanf("%s") per la matricola: si ferma al primo whitespace,
             * quindi va bene per stringhe senza spazi. Per nome e corso
             * usiamo fgets perche' possono contenere spazi. */
            printf("Matricola: "); scanf("%s", mat);
            getchar(); /* consuma il newline dopo scanf, altrimenti il
                        * primo fgets leggerebbe una stringa vuota */
            printf("Nome: ");  fgets(nome,  60, stdin); nome[strcspn(nome, "\n")]   = 0;
            printf("Corso: "); fgets(corso, 60, stdin); corso[strcspn(corso, "\n")] = 0;

            /* registra_studente e' l'unica API utilizzabile: Studente
             * e' un tipo opaco, il main non puo' dichiarare variabili
             * Studente ne' accedere ai suoi campi direttamente. */
            registra_studente(anagrafica, mat, nome, corso);
            break;
        }

        case 2: /* EFFETTUA PRENOTAZIONE */
        {
            int scelta_f;
            printf("Matricola per prenotazione: ");
            scanf("%s", mat);

            /* Quality-of-life: se la matricola non e' registrata, invece
             * di rifiutare seccamente proponiamo la registrazione al volo
             * e poi proseguiamo con la prenotazione. Riduce gli attriti
             * per l'utente che si trova davanti il programma per la
             * prima volta. */
            if (cerca_studente(anagrafica, mat) == NULL) {
                char risp;
                printf("[!] Matricola %s non registrata. Vuoi registrarti ora? (s/n): ", mat);
                scanf(" %c", &risp);  /* spazio iniziale: salta il '\n' precedente */
                getchar();

                if (risp == 's' || risp == 'S') {
                    char nome_preno[60], corso_preno[60];
                    printf("Nome e Cognome: ");
                    fgets(nome_preno, 60, stdin); nome_preno[strcspn(nome_preno, "\n")] = 0;
                    printf("Corso di Studi: ");
                    fgets(corso_preno, 60, stdin); corso_preno[strcspn(corso_preno, "\n")] = 0;

                    registra_studente(anagrafica, mat, nome_preno, corso_preno);
                    printf("[OK] Registrazione completata! Procedo con la prenotazione...\n");
                } else {
                    printf("Operazione annullata.\n");
                    break;
                }
            }

            /* Scelta della fascia oraria. Volutamente lasciamo che valori
             * fuori range (es. 5) finiscano in SERA come fallback: la
             * funzione effettua_prenotazione validera' poi la coerenza
             * con la fascia corrente. */
            printf("Seleziona Fascia (0: Mattina, 1: Pomeriggio, 2: Sera): ");
            scanf("%d", &scelta_f);

            FasciaOraria f_scelta;
            if      (scelta_f == 0) f_scelta = MATTINA;
            else if (scelta_f == 1) f_scelta = POMERIGGIO;
            else                    f_scelta = SERA;

            effettua_prenotazione(anagrafica, aula, coda, mat, f_scelta, ora_attuale);
            break;
        }

        case 3: /* CHECK-IN (INGRESSO) */
        {
            printf("Matricola per check-in: ");
            scanf("%s", mat);

            /* Stessa logica di "registrazione al volo" del case 2:
             * se lo studente arriva fisicamente in aula senza essersi
             * mai registrato, offriamo di farlo subito invece di
             * rimandarlo al menu. */
            if (cerca_studente(anagrafica, mat) == NULL) {
                char risp;
                printf("[!] Studente non trovato. Vuoi registrarlo ora? (s/n): ");
                scanf(" %c", &risp);
                getchar();

                if (risp == 's' || risp == 'S') {
                    char nome_nuovo[60], corso_nuovo[60];
                    printf("Nome e Cognome: ");
                    fgets(nome_nuovo, 60, stdin); nome_nuovo[strcspn(nome_nuovo, "\n")] = 0;
                    printf("Corso di Studi: ");
                    fgets(corso_nuovo, 60, stdin); corso_nuovo[strcspn(corso_nuovo, "\n")] = 0;

                    registra_studente(anagrafica, mat, nome_nuovo, corso_nuovo);
                    printf("[OK] Registrazione completata! Procedo col check-in...\n");
                    effettua_checkin(anagrafica, aula, coda, mat, ora_attuale);
                } else {
                    printf("Operazione annullata.\n");
                }
            } else {
                effettua_checkin(anagrafica, aula, coda, mat, ora_attuale);
            }
            break;
        }

        case 4: /* CHECK-OUT (USCITA) */
            printf("Matricola per check-out: "); scanf("%s", mat);
            effettua_checkout(anagrafica, aula, coda, mat, ora_attuale);
            break;

        case 5: /* VISUALIZZAZIONE STUDENTI PER STATO */
            /* Elenco completo con nome e corso. Per una vista compatta
             * (solo posto + matricola) si potrebbe usare invece
             * visualizza_situazione_corrente. */
            visualizza_studenti_per_stato(anagrafica, aula, coda);
            break;

        case 6: /* ANNULLAMENTO PRENOTAZIONE */
            printf("Matricola per annullamento prenotazione: "); scanf("%s", mat);
            annulla_prenotazione(anagrafica, aula, coda, mat, ora_attuale);
            break;

        case 7: /* REPORT STATO AULA + STORICO */
            genera_report_aula(aula, coda);
            break;

        case 8: /* BATTERIA DI TEST AUTOMATICI */
            /* Modifica pesantemente lo stato del sistema: e' pensata
             * per verifica/demo, non per uso ordinario. */
            esegui_test_completo(anagrafica, aula, coda);
            break;

        case 0: /* USCITA */
            printf("Chiusura sistema...\n");
            /* Liberazione UNICA della memoria: chiamata solo qui, perche'
             * dopo libera_risorse i tre puntatori sono "morti" e non
             * vanno piu' usati (vedi commento in funzioni.c). */
            libera_risorse(anagrafica, aula, coda);
            break;

        default:
            printf("Scelta non valida.\n");
            break;
        }
    } while(scelta != 0);

    return 0;
}