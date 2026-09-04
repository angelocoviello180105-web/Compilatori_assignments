// obiettivo: verificare che due loop adiacenti non guarded vengano fusi correttamente

int main(){
    int x[5], y[5];

    for(int i=0; i<5; i++)
        x[i] = i;

    for(int i=0; i<5; i++)
        y[i] = x[i] + 1;

    return 0;
}