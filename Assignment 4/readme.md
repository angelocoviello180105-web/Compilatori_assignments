# Assignment 4

L'Assignment implementa un passo di Loop Fusion in LLVM.


## Loop Fusion

L'obiettivo del passo è di fondere due cicli adiacenti in un unico ciclo. Questa trasformazione ottimizza l'esecuzione del codice riducendo l'overhead dovuto alla gestione dei salti e delle variabili di controllo del loop, migliorando la località spaziale e temporale dei dati nella cache.

Ad esempio, considerando il seguente codice:
```cpp
for(int i=0; i<100; i++){
   a[i] = i;
}

for(int i=0; i<100; i++){
   b[i] = a[i] + 1;
}
```
I due loop hanno una dipendenza a distanza zero, ma non presentano una negative distance dependence. Pertanto possono essere fusi in questo modo:
```cpp
for(int i=0; i<100; i++){
   a[i] = i;
   b[i] = a[i] + 1;
}
```


## Condizioni per la fusione

Per garantire che la trasformazione sia sicura e non alteri la semantica del programma, il passo analizza le coppie di loop candidate e procede alla fusione solo se i seguenti requisiti sono soddisfatti:
1. **Loop Simplify Form**: entrambi i loop devono presentarsi in forma normale (un singolo preheader, un singolo header, etc.);
2. **Adiacenza**: i loop devono essere immediatamente consecutivi, senza istruzioni intermedie nel flusso di esecuzione. Il passo gestisce coppie di loop *guarded* e *non guarded*;
3. **Control Flow Equivalence**: i due cicli devono essere eseguiti alle stesse esatte condizioni. Nel caso di loop *guarded*, si sfrutta `ScalarEvolution` e il pattern matching dei predicati per garantire che le guardie siano logicamente equivalenti (ad es. `j < cond` e `cond > j`);
4. **Same Trip Count**: entrambi i cicli devono eseguire lo stesso numero di iterazioni, verificato confrontando il backedge-taken count calcolato tramite `ScalarEvolution`;
5. **Assenza di dipendenze negative**: si analizzano le istruzioni che possono leggere o scrivere memoria tramite `mayReadOrWriteMemory()`, per assicurarsi che la fusione non generi *read-after-write* errati (ad es. quando il secondo loop tenta di leggere un dato che il primo loop scriverà in una futura iterazione);
6. **Presenza di variabili di induzione**: i loop devono avere delle *induction variables* canoniche identificabili e non nulle.

L'algoritmo tiene traccia dei loop già elaborati per evitare conflitti o sovrapposizioni nella fusione di catene multiple, garantendo la fusione per coppie disgiunte.


## Note sull'implementazione del CFG

L'implementazione delle funzioni di trasformazione del CFG (`linkLoopPair` e `updateLoopTerminator`) è stata progettata assumendo una specifica struttura canonica dell'IR generato dal front-end. In particolare, il passo di fusione funziona correttamente sotto le seguenti ipotesi:

1. **Indice del branch di uscita**: si assume che l'istruzione di branch condizionale presente nell'`ExitingBlock` (e, se presente, nel blocco della guardia) valuti la condizione di permanenza nel ciclo sul ramo `true` (indice `0`) e la condizione di uscita sul ramo `false` (indice `1`). L'algoritmo reindirizza il successore di indice `1` verso il nuovo blocco di uscita.
2. **Linearità del Body e del Latch**: si assume che il `Body` (blocco relativo al corpo del loop) e il `Latch` siano lineari o presentino un solo blocco predecessore diretto e non ambiguo, permettendo l'uso sicuro di `getSinglePredecessor()`.
3. **Pulizia dei blocchi orfani**: poiché la fusione scollega l'header e il latch del secondo loop dal flusso di esecuzione principale, al termine del passo viene invocata `EliminateUnreachableBlocks()` per rimuovere le istruzioni bypassate (come la vecchia Induction Variable) ed evitare la generazione di errori di dominanza.


## Fasi dell'Ottimizzazione

Se una coppia di loop supera tutti i controlli, si entra nella fase di trasformazione vera e propria, divisa in tre step:
### 1. Aggiornamento dei terminatori (`updateLoopTerminator`)
L'uscita del primo loop (l'*exiting block*) viene modificata affinché punti direttamente all'uscita (*exit block*) del secondo loop. Se i loop sono *guarded*, anche il ramo falso della guardia del primo loop viene reindirizzato verso l'uscita del secondo.
### 2. Aggiornamento delle variabili di induzione (`updateInductionVariables`)
Tutti gli usi della variabile di induzione del secondo loop vengono sostituiti con quelli della variabile di induzione del primo. Successivamente, la variabile di induzione inutilizzata del secondo loop viene rimossa in modo sicuro dall'IR.
### 3. Collegamento dei blocchi (`linkLoopPair`)
I corpi dei due cicli vengono uniti logicamente modificando il CFG. Il predecessore del latch del primo loop (il suo body) viene forzato a saltare al primo body block del secondo loop. A sua volta, l'ultimo body block del secondo loop viene fatto saltare al latch del primo loop, unificando di fatto l'esecuzione in un singolo anello.