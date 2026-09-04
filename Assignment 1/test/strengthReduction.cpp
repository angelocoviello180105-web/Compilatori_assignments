int main(){
    int x = 10;
    int y = 20;

    // test MUL
    int a = x * 15; // 15 = 16 - 1 -> genera: (x << 4) - x
    int b = y * 17; // 17 = 16 + 1 -> genera: (y << 4) + y
    int c = a * 8;  // 8 = 2^3 -> genera: a << 3

    // test DIV
    int d = c / 4;  // 4 = 2^2 -> genera: c >> 2
    int e = b / 8;  // 8 = 2^3 -> genera: b >> 3

    return d + e;
}