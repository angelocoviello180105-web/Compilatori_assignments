// obiettivo: verificare che un'istruzione invariante venga spostata correttamente nel preheader del loop

int main(){
    int x = 10;
    int y = 20;
    int r = 0;

    for(int i=0; i<100; i++){
        // 'x * y' non dipende dall'indice ed è calcolata a partire da variabili definite fuori dal loop
        int inv = x * y;

        r += inv + i;
    }

    return r;
}