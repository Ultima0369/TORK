# TORK v3.14 — π-Heartbeat: 时效·共振·指纹·震荡

AS  = as
LD  = ld
CC  = gcc
CFLAGS = -Wall -Wextra -O2 -Iengine -Iinstinct -Icode -Icore -Iinstall -Isandbox -Ilearning

.PHONY: all clean distclean run install appimage grid torkd start stop status dashboard check-deps test

all: build/tork_core build/tork_engine build/tork_sandbox build/tork_ask build/torkd_start build/tork build/tork_grid
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

build/tork_engine.o: engine/tork_engine.c engine/soul_access.h engine/monitor.h engine/fission.h engine/blackboard.h engine/inductor.h engine/persistor.h engine/idler.h learning/self_cal.h
	$(CC) $(CFLAGS) -Wno-return-type -c -o build/tork_engine.o engine/tork_engine.c

build/monitor.o: engine/monitor.c engine/monitor.h
	$(CC) $(CFLAGS) -c -o build/monitor.o engine/monitor.c

build/fission.o: engine/fission.c engine/fission.h engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/fission.o engine/fission.c

build/instinct.o: instinct/instinct.c instinct/instinct.h
	$(CC) $(CFLAGS) -c -o build/instinct.o instinct/instinct.c

build/code_reader.o: code/code_reader.c code/code_reader.h
	$(CC) $(CFLAGS) -c -o build/code_reader.o code/code_reader.c

build/code_modifier.o: code/code_modifier.c code/code_modifier.h
	$(CC) $(CFLAGS) -c -o build/code_modifier.o code/code_modifier.c

build/blackboard.o: engine/blackboard.c engine/blackboard.h engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/blackboard.o engine/blackboard.c

build/inductor.o: engine/inductor.c engine/inductor.h engine/blackboard.h code/code_reader.h code/code_modifier.h
	$(CC) $(CFLAGS) -c -o build/inductor.o engine/inductor.c

build/persistor.o: engine/persistor.c engine/persistor.h engine/blackboard.h engine/inductor.h
	$(CC) $(CFLAGS) -c -o build/persistor.o engine/persistor.c

build/idler.o: engine/idler.c engine/idler.h engine/blackboard.h engine/inductor.h
	$(CC) $(CFLAGS) -c -o build/idler.o engine/idler.c

# ── Learning modules ──────────────────────────────────────────────

build/experience.o: learning/experience.c learning/experience.h
	$(CC) $(CFLAGS) -c -o build/experience.o learning/experience.c

build/mcts.o: learning/mcts.c learning/mcts.h learning/experience.h
	$(CC) $(CFLAGS) -c -o build/mcts.o learning/mcts.c

build/branch.o: learning/branch.c learning/branch.h learning/experience.h engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/branch.o learning/branch.c

build/pattern.o: learning/pattern.c learning/pattern.h learning/experience.h
	$(CC) $(CFLAGS) -c -o build/pattern.o learning/pattern.c -lm

build/replay.o: learning/replay.c learning/replay.h learning/experience.h learning/pattern.h
	$(CC) $(CFLAGS) -c -o build/replay.o learning/replay.c -lm

build/observer.o: learning/observer.c learning/observer.h
	$(CC) $(CFLAGS) -c -o build/observer.o learning/observer.c -lm

build/snapshot.o: learning/snapshot.c learning/snapshot.h
	$(CC) $(CFLAGS) -c -o build/snapshot.o learning/snapshot.c -lm

build/self_cal.o: learning/self_cal.c learning/self_cal.h
	$(CC) $(CFLAGS) -Wno-return-type -c -o build/self_cal.o learning/self_cal.c -lm

build/energy.o: learning/energy.c learning/energy.h
	$(CC) $(CFLAGS) -c -o build/energy.o learning/energy.c -lm

build/watcher.o: learning/watcher.c learning/watcher.h
	$(CC) $(CFLAGS) -c -o build/watcher.o learning/watcher.c -lm

build/self_build.o: learning/self_build.c learning/self_build.h
	$(CC) $(CFLAGS) -c -o build/self_build.o learning/self_build.c -lm

build/self_tune.o: learning/self_tune.c learning/self_tune.h
	$(CC) $(CFLAGS) -c -o build/self_tune.o learning/self_tune.c

build/mutation_guide.o: learning/mutation_guide.c learning/mutation_guide.h
	$(CC) $(CFLAGS) -c -o build/mutation_guide.o learning/mutation_guide.c -lm

build/distributed.o: learning/distributed.c learning/distributed.h
	$(CC) $(CFLAGS) -c -o build/distributed.o learning/distributed.c -lm

build/pi_seed.o: learning/pi_seed.c learning/pi_seed.h
	$(CC) $(CFLAGS) -c -o build/pi_seed.o learning/pi_seed.c -lm

build/pi_index.o: learning/pi_index.c learning/pi_index.h learning/pi_seed.h
	$(CC) $(CFLAGS) -c -o build/pi_index.o learning/pi_index.c -lm

build/query.o: engine/query.c engine/query.h
	$(CC) $(CFLAGS) -c -o build/query.o engine/query.c -lm

build/task.o: engine/task.c engine/task.h
	$(CC) $(CFLAGS) -c -o build/task.o engine/task.c

build/auditor.o: engine/auditor.c engine/auditor.h
	$(CC) $(CFLAGS) -c -o build/auditor.o engine/auditor.c

build/torkd.o: engine/torkd.c engine/torkd.h
	$(CC) $(CFLAGS) -c -o build/torkd.o engine/torkd.c -lm

# ── Engine link ─────────────────────────────────────────────────────

ENGINE_OBJS = build/tork_engine.o build/monitor.o build/instinct.o build/code_reader.o build/code_modifier.o build/fission.o build/blackboard.o build/self_cal.o build/inductor.o build/persistor.o build/experience.o build/mcts.o build/branch.o build/pattern.o build/replay.o build/observer.o build/snapshot.o build/energy.o build/watcher.o build/query.o build/torkd.o build/self_build.o build/mutation_guide.o build/self_tune.o build/distributed.o build/pi_seed.o build/pi_index.o build/grid_soul_connector.o build/idler.o build/sandbox.o build/agreement.o build/task.o build/auditor.o

build/tork_engine: $(ENGINE_OBJS)
	$(CC) -o build/tork_engine $(ENGINE_OBJS) -lm

# ── Grid ──────────────────────────────────────────────────────────

build/tork_grid.o: grid/tork_grid.c grid/tork_grid.h
	$(CC) $(CFLAGS) -Igrid -c -o build/tork_grid.o grid/tork_grid.c

build/grid_main.o: grid/grid_main.c grid/tork_grid.h
	$(CC) $(CFLAGS) -Igrid -c -o build/grid_main.o grid/grid_main.c

build/grid_soul_connector.o: grid/grid_soul_connector.c grid/grid_soul_connector.h grid/tork_grid.h
	$(CC) $(CFLAGS) -Igrid -c -o build/grid_soul_connector.o grid/grid_soul_connector.c

build/tork_grid: build/grid_main.o build/tork_grid.o build/grid_soul_connector.o
	$(CC) $(CFLAGS) -Igrid -o build/tork_grid build/grid_main.o build/tork_grid.o build/grid_soul_connector.o -lrt

grid: build/tork_grid
	@echo "网格已编译: ./build/tork_grid [帧数]"

# ── CLI tools ──────────────────────────────────────────────────────

build/tork_ask: engine/tork_ask.c build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/torkd.o build/self_build.o build/mutation_guide.o build/self_tune.o build/task.o build/auditor.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o
	$(CC) $(CFLAGS) -o build/tork_ask engine/tork_ask.c build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/torkd.o build/self_build.o build/mutation_guide.o build/self_tune.o build/task.o build/auditor.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o -lm

build/torkd_start: engine/torkd_start.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o
	$(CC) $(CFLAGS) -o build/torkd_start engine/torkd_start.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o -lm

build/tork: engine/tork_cli.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o
	$(CC) $(CFLAGS) -o build/tork engine/tork_cli.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o -lm

torkd: build/torkd_start
	@echo "torkd 已编译: ./build/torkd_start"

# ── Targets ─────────────────────────────────────────────────────────

install: build/tork_engine build/tork_core build/tork_sandbox
	sudo ./install/install.sh

run: build/tork_engine build/tork_core
	./build/tork_engine 10

run100: build/tork_engine build/tork_core
	./build/tork_engine 100

probe: build/probe_env
	@echo "=== 环境探测 ==="
	@./build/probe_env | python3 -m json.tool

clean:
	rm -rf build/*.o build/tork_engine build/tork_core build/tork_sandbox build/tork_grid

distclean: clean
	rm -f soul.bin tork_engine.log persist/*.bin

# ── Dashboard (Python) ───────────────────────────────────────────

dashboard:
	@python3 floating/tork_dashboard.py

check-deps:
	@python3 -c "import tkinter" 2>/dev/null || (echo "需要 tkinter: sudo apt install python3-tk" && exit 1)
	@echo "依赖检查通过"

# ── 完整启动 ────────────────────────────────────────────────────

start: build/tork_engine build/tork_core build/tork_sandbox
	@./tork.sh daemon

stop:
	@./tork.sh stop

status:
	@./tork.sh status

# ── AppImage ──────────────────────────────────────────────────────

appimage: all
	@bash build-installer.sh

# ── Unit Tests ──────────────────────────────────────────────────────

TEST_OBJS = build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o build/task.o build/auditor.o build/experience.o build/pattern.o build/pi_seed.o build/pi_index.o

build/test_core: tests/test_core.c $(TEST_OBJS)
	$(CC) $(CFLAGS) -o build/test_core tests/test_core.c $(TEST_OBJS) -lm

test: build/test_core
	@./build/test_core
