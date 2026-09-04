// obiettivo: verificare che la presenza di istruzioni intermedie tra due loop ne impedisca la fusione

int main(){
    int x[5], y[5];

    for(int i=0; i<5; i++)
        x[i] = i;

    // l'istruzione spezza l'adiacenza dei due loop
    int blocking = x[0] + 1; 

    for(int i=0; i<5; i++)
        y[i] = x[i] + blocking;

    return 0;
}