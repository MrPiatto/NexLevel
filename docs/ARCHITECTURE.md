# NexLevel: come funziona il codice

Guida per capire tutto il progetto, dal livello più alto ai dettagli. Ogni sezione dice **cosa fa**
una parte, **perché** è fatta così e **dove** si trova.

---

## 1. L'idea in una frase

Un giocatore (l'**host**) apre una **stanza** dal suo editor. Gli altri (i **client**) entrano con un
codice. Ogni modifica a un oggetto diventa una **operazione** (op): l'host le mette in un **ordine
unico** e le rimanda a tutti, così ogni editor applica le stesse modifiche nello stesso ordine e il
livello resta identico per tutti.

---

## 2. Struttura delle cartelle

| Cartella | Cosa contiene |
|---|---|
| `core/` | Il "motore": rete, sincronizzazione, cifratura, relay. **Non conosce Geometry Dash**: è C++20 puro, si compila e si testa da solo. |
| `compat/gd2208-geode5/` | La mod vera e propria per GD 2.2081 con Geode v5: hook dell'editor, interfaccia, salvataggi. Usa il core. |
| `deploy/relay/` | Script per installare il server relay su un VPS (Docker, servizio systemd). |
| `tools/` | Script PowerShell: build, test, bot di prova, upload del relay. |
| `docs/` | Guide (online, relay, piano di test). |

**Perché il core è separato:** si può testare senza aprire il gioco (347 controlli automatici) e,
quando uscirà una nuova versione di GD, basta riscrivere solo la parte in `compat/`.

---

## 3. Il modello dei dati (core/sync)

- **NetId** (`netid.hpp`): l'identificativo di un oggetto nella stanza, valido per tutti. È un
  numero a 64 bit = *id del giocatore* + *contatore*. Ognuno crea id nel proprio spazio, quindi due
  giocatori non creano mai lo stesso id.
- **PropMap** (`object_props.hpp`): un oggetto di GD è una mappa *chiave → valore*, la stessa che GD
  usa nei livelli salvati (`1,1,2,15,3,45` = oggetto 1, x=15, y=45). `parseSaveString` e
  `toSaveString` convertono tra testo e mappa; `diffProps` trova cosa è cambiato.
- **Op** (`op.hpp`): tre tipi di operazione: **Create** (nuovo oggetto con le sue proprietà),
  **SetProps** (solo le proprietà cambiate), **Delete**.
- **Replica** (`replica.hpp`): lo stato del livello = tutti gli oggetti + le "lapidi" (tombstones)
  degli oggetti cancellati. Regole: una Create su un id esistente o cancellato viene ignorata, una
  SetProps su un oggetto che non esiste viene ignorata. Così applicare le stesse op nello stesso
  ordine dà sempre lo stesso risultato (determinismo).
- **HostSequencer** (`host_sequencer.hpp`): sull'host assegna a ogni op un **numero di sequenza**
  globale. È l'"arbitro" dell'ordine.
- **ClientReplica** (`client_replica.hpp`): sul client tiene due cose: lo stato **confermato**
  dall'host e le op locali **ancora in attesa** di conferma. La vista mostrata = confermato + in
  attesa. Quando arriva la conferma, l'op passa da "in attesa" a "confermata". Così il giocatore vede
  subito le sue modifiche senza aspettare la rete (*optimistic update*).
- **Impostazioni del livello** (`level_settings.hpp`): colori, sfondo, suolo, canzone ecc. viaggiano
  come **un oggetto speciale** con un id riservato (`kLevelSettingsId`), così usano lo stesso
  meccanismo degli oggetti. Ogni impostazione è una proprietà separata, quindi due persone che
  cambiano impostazioni diverse non si sovrascrivono.
- **Level string** (`level_string.hpp`): costruisce il testo di un livello GD (`intestazione;oggetto;
  oggetto;...`) partendo dalla Replica. Serve per salvare la stanza e per aprirla con il caricatore
  di GD.

---

## 4. La rete (core/net, core/frame, core/session)

- **Asio** (libreria standalone) gestisce i socket TCP su un **thread di rete** separato. Il gioco
  non si blocca mai aspettando la rete.
- **Frame** (`frame.hpp`): ogni messaggio sulla connessione è un *frame* = intestazione (magic,
  versione del protocollo, tipo, lunghezza) + contenuto. `FrameDecoder` ricompone i frame da byte
  che arrivano a pezzi e rifiuta quelli troppo grandi o malformati.
- **Messaggi** (`session/messages.hpp`): Hello (chi sono), Welcome (il tuo id), SnapshotChunk/End
  (il livello quando entri), Submit (op del client), Sequenced (op ordinata dall'host), Roster
  (lista giocatori), Ping/Pong, Presence, PlayerState, ecc. Ogni messaggio ha `encode`/`decode` con
  controlli di validità: un messaggio corrotto viene rifiutato, mai eseguito.
- **ByteWriter/ByteReader** (`bytes.hpp`): scrivono e leggono numeri little-endian e stringhe con
  controllo dei limiti: leggere oltre la fine non crasha, segna l'errore.
- **Versione del protocollo** (`version.hpp`, ora **v8**): se host e client hanno versioni diverse, la
  connessione viene rifiutata con un messaggio chiaro.

### Host (`session/host.cpp`)
1. Apre una porta TCP (47777) e accetta connessioni.
2. Al Hello controlla versione di GD, codice della stanza e numero di giocatori.
3. Manda Welcome + **snapshot** del livello + SnapshotEnd, poi tutte le op nuove in ordine.
4. Ogni op ricevuta (Submit) passa dal sequencer e viene rimandata a tutti (Sequenced).
5. **Limiti anti-abuso**: un *token bucket* limita quante op al secondo può mandare un client, i
   Ping hanno un tetto al secondo.

### Client (`session/client.cpp`)
1. Prova a connettersi a **più destinazioni in parallelo** (diretta, rete locale, relay) e tiene la
   prima che risponde.
2. Riceve lo snapshot, poi applica le op in arrivo.
3. Se la connessione cade, si **riconnette** da solo con un *resume token*: l'host sa quali op aveva
   già ricevuto e non perde niente.

### Snapshot compresso (v1.0.3, `compress.hpp`)
Quando qualcuno entra, il livello viene diviso in pezzi da 512 KB e ogni pezzo è **compresso** con
un algoritmo LZ77 (formato LZ4): cerca sequenze di byte già viste e le sostituisce con "copia N byte
da M posizioni indietro". I dati di GD sono ripetitivi, quindi si riducono circa 7 volte. Il
decompressore controlla ogni lunghezza e posizione, così dati corrotti vengono rifiutati.

---

## 5. Cifratura (core/crypto)

- Libreria **Monocypher**.
- Ogni lato genera una coppia di chiavi **X25519**. Si scambiano le chiavi pubbliche (Hello /
  Secure) e calcolano lo stesso **segreto condiviso** (Diffie-Hellman).
- Dal segreto si ricavano le chiavi con **BLAKE2b**.
- Ogni messaggio successivo è cifrato con **XChaCha20-Poly1305**: cifratura + autenticazione (un
  messaggio modificato viene scartato). Il *nonce* è un contatore implicito, quindi un messaggio
  ripetuto o riordinato non viene accettato.
- Risultato: il relay e chiunque nel mezzo vedono solo dati cifrati.
- Numeri casuali dal sistema operativo: `BCryptGenRandom` su Windows, `getrandom` su Linux,
  `arc4random_buf` su macOS.

---

## 6. Come ci si collega senza IP (codici, UPnP, relay)

- **Codice della stanza** (`room_code.cpp`): impacchetta in poche lettere tutto ciò che serve per
  trovare la stanza (indirizzo o stanza sul relay, porta, chiave, checksum). Tre formati: 13
  caratteri (rete locale), 15 (internet diretto), **10 (relay)**. La checksum fa rifiutare i codici
  scritti male.
- **UPnP** (`net/upnp.cpp`): l'host chiede al router di aprire la porta in automatico (SSDP per
  trovare il router, SOAP per chiedere l'apertura). Gestisce rinnovi, conflitti di porta e router
  che non lo supportano. Non funziona dietro CGNAT (rete mobile), per questo serve il relay.
- **Relay** (`relay/`, server EU-1 su AWS): un server pubblico che fa da ponte. L'host apre una
  stanza sul relay e riceve un numero; il client si collega al relay con quel numero e il relay
  inoltra i byte tra i due (cifrati, quindi il relay non li può leggere). Ha limiti per IP, timeout
  e 90 secondi di tolleranza se l'host si disconnette un attimo.
- **Più relay**: la mod misura il ping verso tutti i relay della lista (`probeRelays`) e usa il più
  veloce. La lista può essere aggiornata da un URL senza aggiornare la mod.

---

## 7. La mod in Geometry Dash (compat/gd2208-geode5/src)

### `editor_tracker.cpp`: il cuore della sincronizzazione nell'editor
- Usa gli **hook** di Geode (`$modify`) per intercettare le funzioni dell'editor: creazione,
  cancellazione, spostamento, rotazione, scala, flip, undo/redo, playtest.
- **Rilevare le modifiche locali**: per ogni oggetto tiene una copia delle proprietà (*cache*). Ogni
  frame confronta gli oggetti toccati con la cache; se qualcosa cambia, manda una SetProps solo con
  le differenze. Uno **sweep** controlla 100 oggetti per frame per trovare modifiche che nessun hook
  ha visto (alcuni strumenti di GD cambiano gli oggetti per vie traverse).
- **Applicare le modifiche degli altri** (`applyRemote`): crea, sostituisce o cancella gli oggetti
  nell'editor. Mentre lo fa ignora i propri hook, per non rimandare indietro la stessa modifica.
- **Sicurezza contro i crash**:
  - ogni oggetto tracciato ha un `retain()`, così il puntatore non diventa mai invalido;
  - mentre stai facendo un gesto (click/tocco premuto, swipe, **Shift**) o hai una finestra di
    modifica aperta, le modifiche degli altri che tolgono o sostituiscono oggetti **aspettano**, e
    le tue partono tutte insieme alla fine;
  - un oggetto selezionato viene prima deselezionato e tolto dal gruppo collegato;
  - l'uscita dei portali di teletrasporto (749) non è sincronizzata da sola: la gestisce GD insieme
    all'entrata (747).
- **Entrare nella stanza** (`adoptFromSession`): invece di cancellare e ricreare tutto, abbina gli
  oggetti già presenti a quelli della stanza **per contenuto** (stessa stringa di proprietà) e crea o
  toglie solo le differenze.
- **Playtest visibile agli altri**: durante il playtest manda ~30 volte al secondo posizione,
  modalità (cubo, nave...), icona e colore del giocatore (`PlayerState`).

### `session_manager.cpp`: la sessione per tutto il gioco
Tiene la stanza (host o client) anche fuori dall'editor, chiama `pump()` ogni frame, mostra le
notifiche (entrato, uscito, riconnesso...), sceglie il codice da mostrare (relay > internet > rete
locale) e collega/scollega l'editor.

### `autosave.cpp`: nessun lavoro perso
- L'host salva la stanza nel suo livello ogni 90 s (5 min per livelli enormi) se è cambiata, anche
  quando è nei menu: il livello viene ricostruito dalla Replica (`buildLevelString`) e compresso
  come fa GD (`ZipUtils::compressString`).
- Salva anche quando l'host esce dall'editor, quando chiude la stanza e quando GD salva il gioco
  (hook di `AppDelegate::trySaveGame`).
- I client hanno una copia di backup "NL - <host>".
- Chiudendo GD da host compare un avviso (hook di `MenuLayer::onQuit`).

### Altri file
| File | Cosa fa |
|---|---|
| `level_settings_adapter.cpp` | Legge e scrive le impostazioni del livello (44 campi, colori, canzone). |
| `presence_overlay.cpp` | Disegna il bordo colorato di dove guarda ogni giocatore, le icone e le scie dei playtest. |
| `nexmenu.cpp` | Il menu NexLevel (schede Room, Players, Network, Settings) e il pulsante nel menu principale. |
| `ui_kit.cpp` | Grafica del menu: rettangoli arrotondati, gradienti, pulsanti, grafico del ping, indicatore di qualità. |
| `ping_hud.cpp` | Scritta del ping nell'editor. |
| `hosting_badge.cpp` | La scritta "Hosting NexLevel room" fuori dall'editor (dentro l'`OverlayManager` di Geode). |
| `relay_directory.cpp` | Lista dei relay (predefinita + scaricata dall'URL). |

---

## 7b. Funzioni di collaborazione (`features.cpp`)
- **Chat**: messaggio `Chat` (client → host → altri). L'host mette l'id vero del mittente, toglie i
  caratteri di controllo, limita a 200 caratteri e a un messaggio ogni 300 ms per giocatore.
- **Solo visione**: l'host tiene un insieme di giocatori "view only"; le loro op vengono rifiutate
  (`Rejected`), quindi sul loro editor la modifica viene annullata. Il flag viaggia nel Roster.
- **Punti di ripristino**: copia della stanza come livello locale "<nome> CP hh:mm" (a mano o ogni
  10 minuti se la stanza è cambiata).
- **Placed by**: l'id di ogni oggetto contiene chi l'ha creato (`NetId::peer()`).
- **Vai da lui**: sposta la telecamera dell'editor sulla posizione di presenza del giocatore.
- **Segnalazione crash**: un file "in-room.marker" esiste mentre sei in una stanza; se al riavvio
  c'è ancora, GD si è chiuso male e NexLevel propone di aprire i log e la pagina dei bug.

## 8. Ping e qualità della connessione (`session/ping.cpp`)
Ogni secondo si manda un Ping; chi risponde dice anche quanto ha aspettato prima di rispondere, che
viene sottratto, così il tempo misurato è solo la rete. Si tengono gli ultimi 60 valori: media,
**jitter** (quanto varia) e **perdita**. Punteggio = media + 2 × jitter + 10 × perdita%, da cui le
fasce Demon / Insane / High / Normal / Bad.

---

## 9. Test e build
- `tools\test-core.ps1`: compila ed esegue i test del core (sincronizzazione con più client
  simulati, riconnessione, cifratura, relay, UPnP con router finto, compressione...).
- I test sono stati eseguiti anche con **AddressSanitizer** (errori di memoria) e
  **ThreadSanitizer** (errori tra thread).
- GitHub Actions ricompila e testa il core su Windows, Linux e macOS a ogni push, compila la mod per
  Windows e, con un tag `vX.Y.Z`, pubblica la release con il file `.geode`.
- `tools\bot.ps1`: un "giocatore finto" da terminale che entra nella stanza, piazza blocchi e
  simula un playtest, per provare senza un secondo PC.

---

## 10. Domande tipiche e risposte brevi

- **Perché un host che ordina le op e non tutti alla pari?** È il modo più semplice per avere
  lo stesso ordine per tutti, e quindi lo stesso livello. Nessun conflitto da risolvere dopo.
- **Cosa succede se due persone modificano lo stesso oggetto?** Vince l'ultima op nell'ordine
  dell'host (*last writer wins*), per ogni singola proprietà.
- **Perché TCP e non UDP?** Le op devono arrivare tutte e in ordine. TCP lo garantisce.
- **Perché serve il relay?** Su rete mobile e con molti provider (CGNAT) non si possono ricevere
  connessioni dall'esterno; il relay è un punto pubblico che entrambi possono raggiungere.
- **Il relay può leggere il livello?** No, inoltra solo dati cifrati end-to-end.
- **Perché i crash "heap corruption"?** GD tiene puntatori grezzi agli oggetti; toglierne uno mentre
  GD lo sta usando (gesto in corso, finestra aperta, portale collegato) lascia un puntatore a memoria
  liberata. Per questo esistono il `retain`, l'attesa durante i gesti e la gestione dei portali.
- **Come mai l'ingresso nei livelli grandi è veloce?** Snapshot compresso (circa 7×) + livello aperto
  con il caricatore di GD + abbinamento per contenuto invece di ricreare tutto.
