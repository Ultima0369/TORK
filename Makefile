# TORK v2.0 — 共生进化系统
# One make to build them all, one make to link them.

AS  = as
LD  = ld
CC  = gcc
CFLAGS = -Wall -Wextra -O2 -Iengine -Iinstinct -Icode -Icore -Iinstall -Isandbox -Ilearning

.PHONY: all clean distclean run install

all: build/tork_core build/tork_engine build/tork_sandbox
	@mkdir -p build
	$(CC) $(CFLAGS) -o build/probe_env install/probe_env.c -lrt

# ── ASM core ────────────────────────────────────────────────────────

build/tork_core.o: core/tork_core.asm core/tork_soul.inc
	mkdir -p build
	$(AS) -I core/ -o build/tork_core.o core/tork_core.asm

build/tork_core: build/tork_core.o
	$(LD) -o build/tork_core build/tork_core.o

# ── Sandbox ─────────────────────────────────────────────────────────

build/sandbox.o: sandbox/sandbox.c sandbox/sandbox.h install/agreement.h
	$(CC) $(CFLAGS) -c -o build/sandbox.o sandbox/sandbox.c

build/agreement.o: install/agreement.c install/agreement.h
	$(CC) $(CFLAGS) -c -o build/agreement.o install/agreement.c

build/tork_sandbox: sandbox/sandbox_cli.c build/sandbox.o build/agreement.o
	$(CC) $(CFLAGS) -o build/tork_sandbox sandbox/sandbox_cli.c build/sandbox.o build/agreement.o

# ── C engine ────────────────────────────────────────────────────────

build/tork_engine.o: engine/tork_engine.c engine/soul_access.h engine/monitor.h engine/fission.h engine/blackboard.h engine/calibrator.h engine/inductor.h engine/persistor.h engine/idler.h
	$(CC) $(CFLAGS) -c -o build/tork_engine.o engine/tork_engine.c

build/monitor.o: engine/monitor.c engine/monitor.h
	$(CC) $(CFLAGS) -c -o build/monitor.o engine/monitor.c

build/fission.o: engine/fission.c engine/fission.h engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/fission.o engine/fission.c

build/instinct.o: instinct/instinct.c instinct/instinct.h engine/calibrator.h
	$(CC) $(CFLAGS) -c -o build/instinct.o instinct/instinct.c

build/code_reader.o: code/code_reader.c code/code_reader.h
	$(CC) $(CFLAGS) -c -o build/code_reader.o code/code_reader.c

build/code_modifier.o: code/code_modifier.c code/code_modifier.h
	$(CC) $(CFLAGS) -c -o build/code_modifier.o code/code_modifier.c

build/blackboard.o: engine/blackboard.c engine/blackboard.h engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/blackboard.o engine/blackboard.c

build/calibrator.o: engine/calibrator.c engine/calibrator.h engine/blackboard.h
	$(CC) $(CFLAGS) -c -o build/calibrator.o engine/calibrator.c

build/inductor.o: engine/inductor.c engine/inductor.h engine/blackboard.h code/code_reader.h code/code_modifier.h
	$(CC) $(CFLAGS) -c -o build/inductor.o engine/inductor.c

build/persistor.o: engine/persistor.c engine/persistor.h engine/blackboard.h engine/calibrator.h engine/inductor.h
	$(CC) $(CFLAGS) -c -o build/persistor.o engine/persistor.c

# ── Learning modules ──────────────────────────────────────────

build/experience.o: learning/experience.c learning/experience.h
	$(CC) $(CFLAGS) -c -o build/experience.o learning/experience.c

build/mcts.o: learning/mcts.c learning/mcts.h learning/experience.h
	$(CC) $(CFLAGS) -c -o build/mcts.o learning/mcts.c


build/idler.o: engine/idler.c engine/idler.h engine/blackboard.h engine/inductor.h
	$(CC) $(CFLAGS) -c -o build/idler.o engine/idler.c

build/tork_engine: build/tork_engine.o build/monitor.o build/instinct.o build/code_reader.o build/code_modifier.o build/fission.o build/blackboard.o build/calibrator.o build/inductor.o build/persistor.o build/experience.o build/mcts.o build/idler.o build/sandbox.o build/agreement.o
	$(CC) -o build/tork_engine build/tork_engine.o build/monitor.o build/instinct.o build/code_reader.o build/code_modifier.o build/fission.o build/blackboard.o build/calibrator.o build/inductor.o build/persistor.o build/experience.o build/mcts.o build/idler.o build/sandbox.o build/agreement.o -lm

# ── Targets ─────────────────────────────────────────────────────────

install: build/tork_engine build/tork_core build/tork_sandbox
	sudo ./install/install.sh

run: build/tork_engine build/tork_core
	./build/tork_engine 10

probe: build/probe_env
	@echo "=== 环境探测 ==="
	@./build/probe_env | python3 -m json.tool

run100: build/tork_engine build/tork_core
	./build/tork_engine 100

clean:
	rm -rf build/*.o build/tork_engine build/tork_core build/tork_sandbox

distclean: clean
	rm -f soul.bin tork_engine.log persist/*.bin

# ── Dashboard (Python, no compilation needed) ───────────────────────

.PHONY: dashboard check-deps

dashboard:
	@echo "🥚 启动 TORK 生命仪表盘..."
	@python3 floating/tork_dashboard.py

check-deps:
	@python3 -c "import tkinter" 2>/dev/null || (echo "⚠️  需要 tkinter: sudo apt install python3-tk" && exit 1)
	@echo "✅ 依赖检查通过"

# ── 完整启动 ────────────────────────────────────────────────────────

.PHONY: start stop status

start: build/tork_engine build/tork_core build/tork_sandbox
	@echo "🥚 TORK 启动中..."
	@./tork.sh daemon

stop:
	@./tork.sh stop

status:
	@./tork.sh status

# ── AppImage (用户分发) ──────────────────────────────
.PHONY: appimage

appimage: all
	@echo "📦 构建 TORK-x86_64.AppImage..."
	@bash build-installer.sh

# ── Grid emergence ───────────────────────────────────────────────
.PHONY: grid

build/tork_grid.o: grid/tork_grid.c grid/tork_grid.h
	$(CC) $(CFLAGS) -c -o build/tork_grid.o grid/tork_grid.c

build/tork_grid: grid/grid_main.c build/tork_grid.o
	$(CC) $(CFLAGS) -o build/tork_grid grid/grid_main.c build/tork_grid.o

grid: build/tork_grid
	@echo "🥚 启动 TORK 网格..."
	@./build/tork_grid 1000
