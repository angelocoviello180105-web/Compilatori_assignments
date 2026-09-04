int main(){
    int x = 10;

    /* primo caso:
        a = x + k
        b = a - k
        => b = x */
    int a = x + 7;
    int b = a - 7;

    /* secondo caso:
        c = b - k
        d = c + k
        => d = b */
    int c = b - 3;
    int d = c + 3;

    return d;
}