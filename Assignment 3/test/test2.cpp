// obiettivo: verificare che il passo sia in grado di spostare istruzioni loop-invariant
    // che dipendono da altre istruzioni loop-invarianti, mantenendo il corretto ordine logico

int main(){
    int x = 10;
    int y = 20;
    int r = 0;

    for(int i=0; i<100; i++){
        // entrambe le istruzioni sono loop-invariant, tuttavia 'b' dipende dal calcolo di 'a'.
        // il passo deve estrarre prima 'a' e poi 'b', altrimenti genererebbe un errore nell'IR
        int a = x + y;
        int b = a * 5; 

        r += b + i;
    }

    return r;
}