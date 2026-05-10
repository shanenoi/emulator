int main(void) {
    volatile int a = 11;
    volatile int b = 20;
    volatile int c = a + b;
    return c;
}
