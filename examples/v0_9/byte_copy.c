static const char source[] = "copy";
static volatile char dest[sizeof(source)];

int main(void) {
    dest[0] = source[0];
    dest[1] = source[1];
    dest[2] = source[2];
    dest[3] = source[3];
    dest[4] = source[4];
    return (int)(dest[0] + dest[1] + dest[2] + dest[3]);
}
