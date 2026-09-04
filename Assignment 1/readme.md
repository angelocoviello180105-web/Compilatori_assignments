# Assignment 1

L'Assignment implementa tre passi di ottimizzazione LLVM.


## 1. Algebraic Identity

Il passo ottimizza:
```cpp
x = x + 0
x = x * 1
```

Step da seguire:
1. controllare che l'operazione sia binaria;
2. distinguere i casi di `ADD` e `MUL`;
   1. per entrambi i casi, estrarre gli operandi e l'eventuale costante;
   2. controllare il valore della costante: se il valore coincide con quello presente nell'Algebraic Identity, si rimpiazza l'operazione con il secondo operando.


## 2. Strength Reduction

Il passo sostituisce operazioni aritmetiche costose con sequenze equivalenti più semplici da eseguire.

Casi gestiti:
```cpp
y = x * 2^k  =>  y = x << k
y = x / 2^k  =>  y = x >> k
y = x * C
```

Con `C` un numero vicino a una potenza di due (in un range di distanza [-2, 2]), in modo da trasformare la `MUL` in una combinazione di `shift` e `ADD/SUB`. Limitare la ricerca a questo range garantisce che il costo delle nuove istruzioni generate sia inferiore o uguale a quello originale della `MUL`.

Step da seguire:
1. controllare che l'istruzione sia un'operazione binaria;
2. distinguere i casi di `MUL` e `DIV`;
   1. per entrambi i casi, estrarre gli operandi, l'eventuale costante e memorizzare l'altro operando;
      - nel caso della `MUL`, cercare una potenza di 2 uguale o vicina a `C` e generare uno `shift` sx con eventuali `ADD/SUB` di compensazione;
      - nel caso della `DIV`, se la costante è una potenza di due, si sostituisce con uno `shift` dx aritmetico;
   2. rimpiazzare gli usi dell'operazione originale con la nuova sequenza di istruzioni.


## 3. Multi-Instruction Optimization

Il passo riconosce sequenze composte da più istruzioni consecutive e le sostituisce con una forma equivalente più semplice.

Casi gestiti:
   ```cpp
   a = b - k
   c = a + k => c = b
   ```
   ```cpp
   a = b + k
   c = a - k => c = b
   ```

Step da seguire per rimuovere l'`ADD` (primo caso):
1. controllare che l'istruzione sia un'`ADD` binaria;
2. verificare che il secondo operando dell'`ADD` sia una costante;
3. controllare che il primo operando dell'`ADD` sia una `SUB` con una costante come secondo operando;
4. se le due costanti coincidono è stata riconosciuta la sequenza ottimizzabile;
5. rimpiazzare gli usi dell'`ADD` con il valore di `b`.

Per rimuovere la `SUB` (secondo caso) i passi sono equivalenti, con la differenza che l'istruzione principale da controllare è una `SUB` e il suo primo operando deve essere un'`ADD` avente una costante come secondo operando.

Si assume che la costante si trovi sempre a destra. Per svincolarsi da questa ipotesi, occorre implementare i rispettivi casi simmetrici.