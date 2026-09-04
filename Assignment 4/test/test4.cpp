// obiettivo: verificare che due loop con trip count differente non vengano fusi

int main(){
    int x[10], y[10];

    for(int i=0; i<5; i++)
        x[i] = i;

    // trip count diverso dal precedente
    for(int i=0; i<10; i++)
        y[i] = x[i] + 1;

    return 0;
}