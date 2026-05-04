# TORK v2.0 — 共生进化系统
# One make to build them all, one make to link them.

AS  = as
LD  = ld
CC  = gcc
CFLAGS = -Wall -Wextra -O2 -Iengine -Iinstinct -Icode -Icore -Iinstall -Isandbox -Ilearning

.PHONY: all clean distclean run install

all: build/tork_core build/tork_engine build/tork_sandbox build/tork_ask build/torkd_start build/tork
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

build/branch.o: learning/branch.c learning/branch.h learning/experience.h engine/soul_access.h

build/pattern.o: learning/pattern.c learning/pattern.h learning/experience.h

build/replay.o: learning/replay.c learning/replay.h learning/experience.h learning/pattern.h

build/observer.o: learning/observer.c learning/observer.h

build/snapshot.o: learning/snapshot.c learning/snapshot.h

build/energy.o: learning/energy.c learning/energy.h

build/watcher.o: learning/watcher.c learning/watcher.h

build/self_build.o: learning/self_build.c learning/self_build.h
	$(CC) $(CFLAGS) -c -o build/self_build.o learning/self_build.c -lm

build/mutation_guide.o: learning/mutation_guide.c learning/mutation_guide.h
	$(CC) $(CFLAGS) -c -o build/mutation_guide.o learning/mutation_guide.c -lm

build/distributed.o: learning/distributed.c learning/distributed.h
	$(CC) $(CFLAGS) -c -o build/distributed.o learning/distributed.c -lm



build/query.o: engine/query.c engine/query.h

build/torkd.o: engine/torkd.c engine/torkd.h
	$(CC) $(CFLAGS) -c -o build/torkd.o engine/torkd.c -lm
	$(CC) $(CFLAGS) -c -o build/query.o engine/query.c -lm

	$(CC) $(CFLAGS) -c -o build/watcher.o learning/watcher.c -lm

	$(CC) $(CFLAGS) -c -o build/energy.o learning/energy.c -lm

	$(CC) $(CFLAGS) -c -o build/snapshot.o learning/snapshot.c -lm

	$(CC) $(CFLAGS) -c -o build/observer.o learning/observer.c -lm

	$(CC) $(CFLAGS) -c -o build/replay.o learning/replay.c -lm

	$(CC) $(CFLAGS) -c -o build/pattern.o learning/pattern.c -lm

	$(CC) $(CFLAGS) -c -o build/branch.o learning/branch.c

	$(CC) $(CFLAGS) -c -o build/mcts.o learning/mcts.c


build/idler.o: engine/idler.c engine/idler.h engine/blackboard.h engine/inductor.h
	$(CC) $(CFLAGS) -c -o build/idler.o engine/idler.c

build/tork_engine: build/tork_engine.o build/monitor.o build/instinct.o build/code_reader.o build/code_modifier.o build/fission.o build/blackboard.o build/calibrator.o build/inductor.o build/persistor.o build/experience.o build/mcts.o build/branch.o build/pattern.o build/replay.o build/observer.o build/snapshot.o build/energy.o build/watcher.o build/query.o build/torkd.o build/self_build.o build/mutation_guide.o build/distributed.o build/grid_soul_connector.o build/idler.o build/sandbox.o build/agreement.o
	$(CC) -o build/tork_engine build/tork_engine.o build/monitor.o build/instinct.o build/code_reader.o build/code_modifier.o build/fission.o build/blackboard.o build/calibrator.o build/inductor.o build/persistor.o build/experience.o build/mcts.o build/branch.o build/pattern.o build/replay.o build/observer.o build/snapshot.o build/energy.o build/watcher.o build/query.o build/torkd.o build/self_build.o build/mutation_guide.o build/distributed.o build/grid_soul_connector.o build/idler.o build/sandbox.o build/agreement.o -lm

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


# ── Grid Emergence ──────────────────────────────────────────────
.PHONY: grid

build/tork_grid.o: grid/tork_grid.c grid/tork_grid.h
	$(CC) $(CFLAGS) -Igrid -c -o build/tork_grid.o grid/tork_grid.c

build/grid_main.o: grid/grid_main.c grid/tork_grid.h
	$(CC) $(CFLAGS) -Igrid -c -o build/grid_main.o grid/grid_main.c


build/grid_soul_connector.o: grid/grid_soul_connector.c grid/grid_soul_connector.h grid/tork_grid.h
	$(CC) $(CFLAGS) -Igrid -c -o build/grid_soul_connector.o grid/grid_soul_connector.c

build/tork_grid: build/grid_main.o build/tork_grid.o build/grid_soul_connector.o
	$(CC) $(CFLAGS) -Igrid -o build/tork_grid build/grid_main.o build/tork_grid.o build/grid_soul_connector.o -lrt

grid: build/tork_grid
	$(CC) -o build/tork_grid build/grid_main.o build/tork_grid.o -lm

grid: build/tork_grid
	@echo "✅ 网格已编译: ./build/tork_grid [帧数]"

build/tork_ask: engine/tork_ask.c build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o
	$(CC) $(CFLAGS) -o build/tork_ask engine/tork_ask.c build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/torkd.o build/self_build.o build/mutation_guide.o -lm

# ── TORK 守护进程 ─────────────────────────────────────────
.PHONY: torkd

build/torkd_start: engine/torkd_start.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/self_build.o build/mutation_guide.o build/distributed.o build/mcts.o build/replay.o
	$(CC) $(CFLAGS) -o build/torkd_start engine/torkd_start.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/self_build.o build/mutation_guide.o build/distributed.o build/mcts.o build/replay.o -lm

build/tork: engine/tork_cli.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/self_build.o build/mutation_guide.o build/distributed.o build/mcts.o build/replay.o
	$(CC) $(CFLAGS) -o build/tork engine/tork_cli.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/self_build.o build/mutation_guide.o build/distributed.o build/mcts.o build/replay.o -lm

torkd: build/torkd_start
	@echo "✅ torkd 已编译: ./build/torkd_start"
