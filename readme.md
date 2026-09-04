# Compilatori Assignments - Coviello Angelo

Il repository contiene gli Assignments del corso di Compilatori.


## Prerequisiti

Per compilare ed eseguire un test è necessario installare:
- `CMake` e `Make`
- `LLVM` e `Clang`


## Compilazione dei passi di ottimizzazione

- Dopo essersi posizionati nella root directory dell'assignment, compilare i passi:
    ```bash
    mkdir build && cd build
    cmake -DLT_LLVM_INSTALL_DIR=<LLVM_PATH> ../src
    make
    ```
    > Sostituire `<LLVM_PATH>` con il percorso assoluto dell'installazione locale di LLVM (es. `/usr/lib/llvm-19`), ottenibile tramite il comando: `llvm-config --prefix`


## Preparazione di un test

1. Posizionarsi nella directory del test che si vuole eseguire:
    ```bash
    cd ../test
    ```

2. Generare l'IR (senza ottimizzazioni e senza `optnone`):
    ```bash
    clang -O0 -Xclang -disable-O0-optnone -emit-llvm -S -c <nome_test>.cpp -o main.ll
    ```

3. Applicare `mem2reg`:
    ```bash
    opt -passes=mem2reg main.ll -S -o main.m2r.ll
    ```


## Esecuzione di un test

- Dopo essersi posizionati nella directory del test, eseguire l'ottimizzatore:
    ```bash
    opt -S -load-pass-plugin ../build/<nome_libreria>.so -p <nome_passo> main.m2r.ll -o main.optimized.ll
    ```