/* obiettivo: verificare che due loop con condizioni di guardia differenti
    (control flow non equivalente) non vengano fusi */

int main() {
    int x[5], y[5];
    int j = 0;
    int i = 0;

    if(j < 5){
        do{
            x[i] = i;
            i++;
        }while(i < 5);
    }

    // diverso flusso di controllo: '<=' invece di '<'
    if(j <= 5){
        i = 0;
        do{
            y[i] = x[i] + 1;
            i++;
        }while(i < 5);
    }

    return 0;
}