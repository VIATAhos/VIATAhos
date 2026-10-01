# Guida del Programmatore e Architettura Hardware - VIATAhos

Questo manuale descrive le specifiche hardware e software dell'architettura supportata dal sistema operativo VIATAhos e la struttura dei comandi e dei prompt della shell T-CORE.

---

## 1. Architetture Supportate (x86, x64, ARM)

Per combattere l'obsolescenza programmata e restituire il controllo hardware agli utenti, VIATAhos è progettato nativamente per le architetture PC standard:
- **32-bit Legacy (x86):** Per il recupero di vecchi computer e portatili (Pentium, Core 2, vecchi AMD).
- **64-bit (x64):** L'architettura principale per i computer moderni, garantendo l'accesso completo a grandi quantità di RAM e registri larghi.
- **ARM a basso consumo:** Supportato per dispositivi embedded, Raspberry Pi e smartphone (come il VIATAconnected), ottimizzando l'efficienza energetica della batteria.

Il kernel è scritto in C puro e Assembly (x86/ARM) e gestisce l'hardware direttamente tramite accesso low-level, aggirando le astrazioni forzate dei moderni BIOS/UEFI ove possibile.

---

## 2. Gestione Memoria (Ring 0 / Flat Memory Model)

VIATAhos rifiuta la complessa parcellizzazione dei moderni SO commerciali:
- Funziona interamente in modalità **Ring 0** (privilegi kernel totali), offrendo ai programmi (se eseguiti da Amministratore) l'accesso diretto e senza filtri alla RAM fisica e alle porte I/O.
- **Flat Memory Model:** Lo spazio di indirizzamento non è segmentato. Per l'architettura a 64-bit, un'applicazione `.xep` può teoricamente accedere a tutta la memoria fisica disponibile in modo lineare.

---

## 3. Gerarchia dei Prompt e dei Drive in VIATAhos

Il prompt del terminale cambia dinamicamente per mostrare il tipo di supporto di boot utilizzato, lo stato dei permessi amministratore (Admin Mode `KN`) e il disco correntemente selezionato.

Le lettere **`A`, `B`, `C`, `D`** sono i **4 e soli** tipi di supporto base. Tutti gli altri drive aggiuntivi (numerati F, D, R, X) sono dischi formattati per VIATAhos, accessibili come drive secondari.

| Lettera | Tipo Hardware | Accesso OS | Scrivibile |
|---|---|---|---|
| `A:` | Floppy Disk (Fisico o Emulato USB) | ✅ Può contenere l'OS | ❌ Read-Only (OS protetto) |
| `B:` | CD / DVD / Blu-Ray | ✅ Può contenere l'OS | ❌ Read-Only (solo masterizzazione) |
| `C:` | Rimovibile (USB/SD) | ✅ Può contenere l'OS | ✅ Scrivibile |
| `D:` | Disco Fisso Interno (HDD/SSD/NVMe) | ✅ Può contenere l'OS | ✅ Scrivibile |
| `E:` | Nastro Magnetico (Tape) | ❌ Solo Dati/Dump | ✅ Scrivibile sequenzialmente |
| `F:` | Disco Virtuale (RAM Disk/Immagine) | ✅ Avvio Virtualizzato | ✅ Scrivibile in RAM |

---

## 4. Programmazione di Sistema

L'utente programma il sistema usando standard aperti:
- **C e Assembly x86/ARM:** Il kernel VIATAhos è compatibile con codice macchina nativo.
- Accesso Diretto all'Hardware: Per esempio, la scrittura diretta nel framebuffer VESA/VGA (es. all'indirizzo `0xA0000` o nel LFB) per la gestione della grafica, saltando del tutto l'uso di pesanti driver 3D o API chiuse.

---

## 5. Estensioni dei File Standard

Per garantire compatibilità e ordine all'interno del file system (FAT32/exFAT), vengono definite estensioni ufficiali rigide:

### `.kvbn` (Kernel Viata BiN)
*   Script di sistema e batch. Contengono sequenze di comandi testuali interpretati direttamente dal Kernel.

### `.xep` (eXEcutable aPplication)
*   Applicazioni e programmi binari nativi (formato ELF-like) compilati per x86/x64/ARM. Vengono caricati in RAM ed eseguiti direttamente dalla CPU.

### `.vlib` (Viata LIBrary)
*   Librerie condivise caricate dinamicamente in RAM usate dalle applicazioni.
*   **Sistema Universale Datatypes (`datatype.vlib`):** Agisce come decodificatore universale dei formati di file per tutto il sistema operativo.

### `.smol` (Seconda Estensione — File Compresso a Decompressione Trasparente)
*   Estensione aggiuntiva (seconda) che segnala che il file è compresso (LZ77). **Non sostituisce l'estensione originale**: `editor.xep.smol` o `foto.png.smol`.
*   È disponibile il tool companion `unsmol.exe` (per Windows) o `unsmol_linux` / `unsmol_mac` che viene copiato automaticamente sulle chiavette USB per aprire i file da qualsiasi computer.

---

## 6. Gestione File e Metadati Ibridi (File Ombra)

Per mantenere compatibilità Plug & Play al 100% con Windows/Linux:
- **File Principale:** I file (es. `testo.txt`) rimangono standard FAT32.
- **File Ombra:** VIATAhos crea una cartella di sistema nascosta `\.viata\` nella radice del disco per ospitare permessi, metadati, e icone custom. Inserendo il drive in un computer moderno, i file restano perfetti e accessibili, ignorando semplicemente la cartella nascosta.

---

## 7. Filosofia Architetturale: Differenze da UNIX (Anti-UNIX)

Il sistema operativo VIATAhos rifiuta la filosofia di design di UNIX:

### A. Risorse Hardware Fisiche (No "Tutto è un file")
In VIATAhos le risorse hardware non sono mascherate come file sotto `/dev`. L'accesso avviene tramite Memory-Mapped I/O o istruzioni di porta (`in/out` su x86).

### B. Gerarchia dei Drive (No Root Unica `/`)
Niente `mount` astratto. VIATAhos mostra la reale separazione fisica tramite le storiche lettere di drive (`A:\>`, `C:\>`), rispecchiando la vera struttura della macchina.

### C. File Fortemente Tipizzati
L'estensione determina in modo assoluto come il Kernel gestisce il file. Non esistono file magicamente eseguibili per via di un flag (come in `chmod +x`).

---

## 9. Interfaccia: Terminale T-CORE e GUI

- **T-CORE (TUI Multitasking):** L'utente può premere i tasti da **F1** a **F12** per passare istantaneamente tra 12 sessioni di terminale (shell) indipendenti e parallele.
- **GUI (comando `tsi`):** Un window manager nativo che richiama lo stile AmigaOS o Windows 3.x. Finestre bisellate 3D, pattern di sfondo in pixel art e palette ristretta per una massima fluidità su vecchi PC.

---
## 9. Rete P2P Decentralizzata: VIATAlink e VIATAshare

- **Condivisione di Prossimità (VIATAshare):** Utilizza moduli di rete per condividere file `.smol` direttamente tra dispositivi senza internet (comando `shr`).
- **P2P Globale (VIATAlink):** Una DHT decentralizzata basata sulle chiavi pubbliche crittografiche (generate dal comando `key`). Permette telefonate VoIP (`call nickname`), invio messaggi (`msg`) e file sharing a distanza, tutto crittografato *end-to-end* e slegato dai server Cloud corporativi.

---

## 10. L'Ecosistema VIATA: Recupero e Sopravvivenza Digitale

La vera essenza del progetto (basato a Cuneo e Dronero) è l'attivismo "Right to Repair":
- **VIATAstation:** Laptop e vecchi ThinkPad salvati dalle discariche, resuscitati con VIATAhos per ridare loro velocità e utilità senza l'ingombro dei sistemi moderni.
- **VIATAconnected:** Cellulari feature-phone e dispositivi ARM modificati per far girare il core di VIATAhos, rendendoli terminali di comunicazione P2P non tracciabili.
