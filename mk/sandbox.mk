# Auto-generated from Makefile
# Sections: Sandbox, Sandbox Launcher (namespace isolation)

# ── Sandbox ─────────────────────────────────────────────────────────

build/sandbox.o: src/sandbox/sandbox.c src/sandbox/sandbox.h src/install/agreement.h
	$(CC) $(CFLAGS) -c -o build/sandbox.o src/sandbox/sandbox.c

build/agreement.o: src/install/agreement.c src/install/agreement.h
	$(CC) $(CFLAGS) -c -o build/agreement.o src/install/agreement.c

build/growth_node.o: src/learning/growth_node.c src/learning/growth_node.h
	$(CC) $(CFLAGS) -c -o build/growth_node.o src/learning/growth_node.c

build/tork_sandbox: src/sandbox/sandbox_cli.c build/sandbox.o build/agreement.o
	$(CC) $(CFLAGS) -o build/tork_sandbox src/sandbox/sandbox_cli.c build/sandbox.o build/agreement.o


# ── Sandbox Launcher (namespace isolation) ────────────────────────────

build/tork_sandbox_launcher: sandbox/tork_sandbox.c
	$(CC) -Wall -Wextra -O2 -o build/tork_sandbox_launcher sandbox/tork_sandbox.c

sandbox: build/tork_sandbox_launcher build/tork_engine build/tork_core
	@echo "沙箱启动器已编译: sudo ./build/tork_sandbox_launcher"


