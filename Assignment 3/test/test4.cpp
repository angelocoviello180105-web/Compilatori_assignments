// obiettivo: verificare che un'istruzione loop-invariant non venga spostata se non domina le uscite e non è dead

int main(){
    int x = 10;
    int y = 20;
    int r = 0;
    int inv = 0;

    for(int i=0; i<100; i++){
        if(i%2 == 0){
            // è invariante ma è dentro un if, quindi non domina l'uscita
            inv = x * y; 
            r += i;
        }
    }

    // 'inv' non è dead dopo il loop, dato che viene utilizzata.
    // il passo riconosce che non è sicuro spostarla e la lascia dov'è
    return r + inv; 
}