static volatile int fib_input = 10;

static int fib_iter(int n) {
    int a = 0;
    int b = 1;
    for (int i = 0; i < n; i++) {
        int next = a + b;
        a = b;
        b = next;
    }
    return a;
}

int main(void) {
    return fib_iter(fib_input);
}
