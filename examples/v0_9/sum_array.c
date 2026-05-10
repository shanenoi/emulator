static volatile int values[] = {1, 2, 3, 4};
static volatile int zeroes[4];

int main(void) {
    int sum = zeroes[0];
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        sum += values[i];
    }
    return sum;
}
