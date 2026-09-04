// obiettivo: verificare che due loop non vengano fusi se presentano una dipendenza negativa (read-after-write)

int main(){
    int x[6], y[5];

    for(int i=0; i<5; i++)
        x[i] = i;

    for(int i=0; i<5; i++)
        // dipendenza negativa: 'y' dipende da un valore di 'x' spostato in avanti
        y[i] = x[i+1] + 1; 

    return 0;
}