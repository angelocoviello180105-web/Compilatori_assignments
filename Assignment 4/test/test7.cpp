/* obiettivo: verificare la corretta elaborazione di coppie multiple di
    loop senza che i loop già fusi generino conflitti */

int main() {
    int x[5], y[5], w[5], z[5];

    // prima coppia: fusione di due loop consecutivi
    for(int i=0; i<5; i++)
        x[i] = i;

    for(int i=0; i<5; i++)
        y[i] = x[i] + 1;


    // seconda coppia: fusione di due loop consecutivi senza conflitti con la prima coppia
    for(int i=0; i<5; i++)
        w[i] = y[i] + 1;

    for(int i=0; i<5; i++)
        z[i] = w[i] + 1;

    return 0;
}