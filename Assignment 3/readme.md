# Assignment 3

L'Assignment implementa un passo di Loop Invariant Code Motion (LICM) in LLVM.


## Loop Invariant Code Motion (LICM)

L'obiettivo del passo è di spostare, quando possibile, le istruzioni fuori da un loop. Così facendo le operazioni che non dipendono dalle iterazioni vengono eseguite una sola volta anziché una per iterazione, migliorando le prestazioni e ottimizzando l’esecuzione del codice.

Ad esempio, considerando il seguente codice:
```cpp
for(int i=0; i<100; i++){
   int x = 3;
   int y = x + i;
}
```
Si può notare che l'istruzione `int x = 3;` non dipende dalla variabile di iterazione `i` e può essere spostata fuori dal loop, risultando in:
```cpp
int x = 3;
for(int i=0; i<100; i++){
   int y = x + i;
}
```


## Il passo di ottimizzazione è composto da tre fasi principali:


### 1. Identificazione delle istruzioni loop-invariant
In questa fase, l'algoritmo ispeziona tutte le istruzioni contenute nei BB del loop. Un'istruzione viene classificata come loop-invariant se soddisfa i seguenti requisiti:
- è un'operazione binaria (vengono esclusi preventivamente `PHINode`, istruzioni di terminazione come salti o ritorni, e chiamate a funzione per evitare *side-effects*);
- tutti i suoi operandi sono definiti al di fuori del loop, oppure sono costanti, oppure sono calcolati da altre istruzioni già identificate come *loop-invariant* all'interno del ciclo stesso. 

Il passo utilizza una logica ricorsiva per navigare e validare questa catena di dipendenze.

### 2. Identificazione delle istruzioni che possono essere spostate (*movable*)
Non tutte le istruzioni loop-invariant possono essere estratte dal loop in modo sicuro. Lo spostamento è possibile solo se l'istruzione soddisfa almeno una di queste due condizioni:
- **Domina tutte le uscite del loop**: l'istruzione verrebbe eseguita in qualsiasi percorso di esecuzione che porti all'uscita del ciclo.
- **È *dead* dopo il loop**: il valore prodotto dall'istruzione non viene mai letto o utilizzato all'esterno del loop. Di conseguenza, eseguirla anticipatamente fuori dal ciclo non corromperà lo stato del programma.

### 3. Esecuzione della fase di code motion
Le istruzioni identificate come sicure (*movable*) vengono spostate nel **preheader** del loop, un blocco speciale che precede l'header e viene eseguito una sola volta prima di entrare nel ciclo.

Per garantire la corretta generazione dell'Intermediate Representation (IR) ed evitare errori di uso prima della definizione, l'algoritmo:
   1. inizializza una *worklist* contenente tutte le istruzioni candidate allo spostamento;
   2. itera sulla coda analizzando gli operandi di ciascuna istruzione: se l'istruzione dipende da valori che si trovano all'interno del loop, la lascia in coda;
   3. se l'istruzione ha le dipendenze libere (già spostate nel preheader o definite fuori dal ciclo), sposta l'istruzione nel preheader e la rimuove definitivamente dalla worklist;
   4. il ciclo si ripete finché tutte le dipendenze non vengono progressivamente soddisfatte, svuotando la worklist.