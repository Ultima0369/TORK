#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("TORK - The Organism That Runs and Knows\n");
        printf("Usage: TORK-x86_64.AppImage [--status|--stop]\n");
        return 0;
    }
    char *h = getenv("HOME");
    if (!h) return 1;
    char t[4096];
    snprintf(t, sizeof(t), "%s/.local/share/tork", h);
    char m[4096];
    snprintf(m, sizeof(m), "%s/.extracted", t);
    if (access(m, F_OK) != 0) {
        char s[4096];
        ssize_t l = readlink("/proc/self/exe", s, sizeof(s) - 1);
        if (l < 0) return 1;
        s[l] = 0;
        char c[8192];
        snprintf(c, sizeof(c),
                 "mkdir -p '%s' && cd '%s' && tail -c +OFFSET '%s' | tar xzf - && touch .extracted",
                 t, t, s);
        int rc = system(c);
        if (rc != 0) return 1;
        printf("TORK extracted to %s\n", t);
    }
    if (argc > 1) {
        char r[4096];
        snprintf(r, sizeof(r), "%s/run.sh", t);
        char *args[argc + 2];
        args[0] = "bash";
        args[1] = r;
        for (int i = 1; i < argc; i++) args[i + 1] = argv[i];
        args[argc + 1] = NULL;
        execv("/bin/bash", args);
        return 1;
    }
    char r[4096];
    snprintf(r, sizeof(r), "%s/run.sh", t);
    execl("/bin/bash", "bash", r, NULL);
    return 1;
}
