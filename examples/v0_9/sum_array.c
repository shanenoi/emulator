static volatile int values[] = {1, 2, 3, 4, 5};
static volatile int zeroes[4];

int main(void) {
    int sum = zeroes[0];
    volatile int *p = values;
    volatile int *end = values + 5;
    while (p != end) {
        sum += *p;
        p++;
    }
    return sum;
}
