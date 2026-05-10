static volatile int values[] = {1, 2, 3, 4, 5};
static volatile int zeroes[4];

int main(void) {
    int sum = zeroes[0];
    sum += values[0];
    sum += values[1];
    sum += values[2];
    sum += values[3];
    sum += values[4];
    return sum;
}
