static int add2(int value) {
    return value + 2;
}

static int add3(int value) {
    return add2(value) + 1;
}

int main(void) {
    return add3(7);
}
