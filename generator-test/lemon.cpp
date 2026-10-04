#include <stdio.h>

int main(int argc, char **argv) {
    if (argc >= 3 && argv[1][0] == '-' && argv[1][1] == '-') {
        FILE *f = fopen(argv[2], "w");
        if (!f) return 1;
        fputs("int generated_value() { return 42; }\n", f);
        fclose(f);
        return 0;
    }
    return 1;
}
