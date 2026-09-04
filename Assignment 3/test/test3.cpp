// obiettivo: verificare lo spostamento di un'istruzione che non domina tutte le uscite,
    // ma il cui risultato è dead all'interno del ciclo

int main(){
    int x = 10;
    int r = 0;

    for(int i=0; i<100; i++){
        // essendo l'istruzione in un if, non domina le uscite del loop.
        // potrebbe non essere mai eseguita se la condizione è falsa
        if(i%2 == 0){
            /* l'istruzione è loop-invariant, non domina le uscite, ma viene comunque estratta
                dato che il passo verifica che 'y' non venga mai utilizzata fuori dal loop */
            int y = x * 42; 
            r += y;
        }
    }

    return r;
}