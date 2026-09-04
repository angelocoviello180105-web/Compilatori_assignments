/* obiettivo: verificare che due loop adiacenti e guarded con
    condizioni logicamente equivalenti vengano fusi correttamente */

int main(){
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

    // condizione logicamente equivalente alla precedente ma invertita
    if(cond > j){
        i = 0;
        do{
            y[i] = x[i] + 1;
            i++;
        }while(i < 5);
    }

    return 0;
}