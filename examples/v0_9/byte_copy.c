static const char source[] = "copy";
static volatile char dest[sizeof(source)];

int main(void) {
    for (unsigned i = 0; i < (unsigned)sizeof(source); i++) {
        dest[i] = source[i];
    }
    return (int)(dest[0] + dest[1] + dest[2] + dest[3]);
}
