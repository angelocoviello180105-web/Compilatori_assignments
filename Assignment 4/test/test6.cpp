/* obiettivo: verificare che due loop con condizioni di guardia differenti
    (control flow non equivalente) non vengano fusi */

int main() {
    int x[5], y[5];
    int cond = 5;
    int j = 0;
    int i = 0;

    if(j < cond){
        do{
            x[i] = i;
            i++;
        }while(i < 5);
    }

    // flusso di controllo differente: '<=' invece di '<'
    if(j <= cond){
        i = 0;
        do{
            y[i] = x[i] + 1;
            i++;
        }while(i < 5);
    }

    return 0;
}