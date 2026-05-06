# TORK v3.15 — TLN: 三进制逻辑推理 + π-Heartbeat

AS  = as
LD  = ld
CC  = gcc
CFLAGS = -Wall -Wextra -O2 -Isrc/engine -Isrc/instinct -Isrc/code -Isrc/core -Isrc/install -Isrc/sandbox -Isrc/learning

.PHONY: all clean distclean run install appimage grid torkd start stop status dashboard check-deps test sandbox

all: build/tork_core build/tork_engine build/tork_sandbox build/tork_sandbox_launcher build/tork_ask build/torkd_start build/tork build/tork_grid
	@mkdir -p build
	$(CC) $(CFLAGS) -o build/probe_env src/install/probe_env.c -lrt

# ── ASM core ────────────────────────────────────────────────────────

build/tork_core.o: src/core/tork_core.asm src/core/tork_soul.inc
	mkdir -p build
	$(AS) -I src/core/ -o build/tork_core.o src/core/tork_core.asm

build/tork_core: build/tork_core.o
	$(LD) -o build/tork_core build/tork_core.o

# ── Sandbox ─────────────────────────────────────────────────────────

build/sandbox.o: src/sandbox/sandbox.c src/sandbox/sandbox.h src/install/agreement.h
	$(CC) $(CFLAGS) -c -o build/sandbox.o src/sandbox/sandbox.c

build/agreement.o: src/install/agreement.c src/install/agreement.h
	$(CC) $(CFLAGS) -c -o build/agreement.o src/install/agreement.c

build/tork_sandbox: src/sandbox/sandbox_cli.c build/sandbox.o build/agreement.o
	$(CC) $(CFLAGS) -o build/tork_sandbox src/sandbox/sandbox_cli.c build/sandbox.o build/agreement.o

# ── Sandbox Launcher (namespace isolation) ────────────────────────────

build/tork_sandbox_launcher: sandbox/tork_sandbox.c
	$(CC) -Wall -Wextra -O2 -o build/tork_sandbox_launcher sandbox/tork_sandbox.c

sandbox: build/tork_sandbox_launcher build/tork_engine build/tork_core
	@echo "沙箱启动器已编译: sudo ./build/tork_sandbox_launcher"

# ── C engine ────────────────────────────────────────────────────────

build/tork_engine.o: src/engine/tork_engine.c src/engine/soul_access.h src/engine/monitor.h src/engine/fission.h src/engine/blackboard.h src/engine/inductor.h src/engine/persistor.h src/engine/idler.h src/learning/self_cal.h
	$(CC) $(CFLAGS) -Wno-return-type -c -o build/tork_engine.o src/engine/tork_engine.c

build/monitor.o: src/engine/monitor.c src/engine/monitor.h
	$(CC) $(CFLAGS) -c -o build/monitor.o src/engine/monitor.c

build/fission.o: src/engine/fission.c src/engine/fission.h src/engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/fission.o src/engine/fission.c

build/instinct.o: src/instinct/instinct.c src/instinct/instinct.h
	$(CC) $(CFLAGS) -c -o build/instinct.o src/instinct/instinct.c

build/code_reader.o: src/code/code_reader.c src/code/code_reader.h
	$(CC) $(CFLAGS) -c -o build/code_reader.o src/code/code_reader.c

build/tln.o: src/engine/tln.c src/engine/tln.h
	$(CC) $(CFLAGS) -c -o build/tln.o src/engine/tln.c

build/code_archive.o: src/persist/code_archive.c src/persist/code_archive.h
	$(CC) $(CFLAGS) -c -o build/code_archive.o src/persist/code_archive.c

build/strict_verifier.o: src/code/strict_verifier.c src/code/strict_verifier.h
	$(CC) $(CFLAGS) -c -o build/strict_verifier.o src/code/strict_verifier.c

build/code_modifier.o: src/code/code_modifier.c src/code/code_modifier.h
	$(CC) $(CFLAGS) -c -o build/code_modifier.o src/code/code_modifier.c

build/blackboard.o: src/engine/blackboard.c src/engine/blackboard.h src/engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/blackboard.o src/engine/blackboard.c

build/inductor.o: src/engine/inductor.c src/engine/inductor.h src/engine/blackboard.h src/code/code_reader.h src/code/code_modifier.h
	$(CC) $(CFLAGS) -c -o build/inductor.o src/engine/inductor.c

build/persistor.o: src/engine/persistor.c src/engine/persistor.h src/engine/blackboard.h src/engine/inductor.h
	$(CC) $(CFLAGS) -c -o build/persistor.o src/engine/persistor.c

build/idler.o: src/engine/idler.c src/engine/idler.h src/engine/blackboard.h src/engine/inductor.h
	$(CC) $(CFLAGS) -c -o build/idler.o src/engine/idler.c

# ── Learning modules ──────────────────────────────────────────────

build/experience.o: src/learning/experience.c src/learning/experience.h
	$(CC) $(CFLAGS) -c -o build/experience.o src/learning/experience.c

build/mcts.o: src/learning/mcts.c src/learning/mcts.h src/learning/experience.h
	$(CC) $(CFLAGS) -c -o build/mcts.o src/learning/mcts.c

build/branch.o: src/learning/branch.c src/learning/branch.h src/learning/experience.h src/engine/soul_access.h
	$(CC) $(CFLAGS) -c -o build/branch.o src/learning/branch.c

build/pattern.o: src/learning/pattern.c src/learning/pattern.h src/learning/experience.h
	$(CC) $(CFLAGS) -c -o build/pattern.o src/learning/pattern.c -lm

build/replay.o: src/learning/replay.c src/learning/replay.h src/learning/experience.h src/learning/pattern.h
	$(CC) $(CFLAGS) -c -o build/replay.o src/learning/replay.c -lm

build/observer.o: src/learning/observer.c src/learning/observer.h
	$(CC) $(CFLAGS) -c -o build/observer.o src/learning/observer.c -lm

build/snapshot.o: src/learning/snapshot.c src/learning/snapshot.h
	$(CC) $(CFLAGS) -c -o build/snapshot.o src/learning/snapshot.c -lm

build/self_cal.o: src/learning/self_cal.c src/learning/self_cal.h
	$(CC) $(CFLAGS) -Wno-return-type -c -o build/self_cal.o src/learning/self_cal.c -lm

build/energy.o: src/learning/energy.c src/learning/energy.h
	$(CC) $(CFLAGS) -c -o build/energy.o src/learning/energy.c -lm

build/watcher.o: src/learning/watcher.c src/learning/watcher.h
	$(CC) $(CFLAGS) -c -o build/watcher.o src/learning/watcher.c -lm

build/self_build.o: src/learning/self_build.c src/learning/self_build.h
	$(CC) $(CFLAGS) -c -o build/self_build.o src/learning/self_build.c -lm

build/self_tune.o: src/learning/self_tune.c src/learning/self_tune.h
	$(CC) $(CFLAGS) -c -o build/self_tune.o src/learning/self_tune.c

build/mentor.o: src/learning/mentor.c src/learning/mentor.h src/learning/self_tune.h
	$(CC) $(CFLAGS) -c -o build/mentor.o src/learning/mentor.c

build/mutation_guide.o: src/learning/mutation_guide.c src/learning/mutation_guide.h
	$(CC) $(CFLAGS) -c -o build/mutation_guide.o src/learning/mutation_guide.c -lm

build/distributed.o: src/learning/distributed.c src/learning/distributed.h
	$(CC) $(CFLAGS) -c -o build/distributed.o src/learning/distributed.c -lm

build/pi_seed.o: src/learning/pi_seed.c src/learning/pi_seed.h
	$(CC) $(CFLAGS) -c -o build/pi_seed.o src/learning/pi_seed.c -lm

build/pi_index.o: src/learning/pi_index.c src/learning/pi_index.h src/learning/pi_seed.h
	$(CC) $(CFLAGS) -c -o build/pi_index.o src/learning/pi_index.c -lm

build/query.o: src/engine/query.c src/engine/query.h
	$(CC) $(CFLAGS) -c -o build/query.o src/engine/query.c -lm

build/task.o: src/engine/task.c src/engine/task.h
	$(CC) $(CFLAGS) -c -o build/task.o src/engine/task.c

build/auditor.o: src/engine/auditor.c src/engine/auditor.h
	$(CC) $(CFLAGS) -c -o build/auditor.o src/engine/auditor.c

build/dispatch.o: src/engine/dispatch.c src/engine/dispatch.h
	$(CC) $(CFLAGS) -c -o build/dispatch.o src/engine/dispatch.c

build/codegen.o: src/engine/codegen.c src/engine/codegen.h
	$(CC) $(CFLAGS) -c -o build/codegen.o src/engine/codegen.c

build/scheduler.o: src/engine/scheduler.c src/engine/scheduler.h src/engine/soul_access.h src/instinct/instinct.h
	$(CC) $(CFLAGS) -Wno-return-type -c -o build/scheduler.o src/engine/scheduler.c

build/sched_services.o: src/engine/sched_services.c src/engine/sched_services.h src/engine/sched_tln.h src/engine/scheduler.h
	$(CC) $(CFLAGS) -c -o build/sched_services.o src/engine/sched_services.c

build/sched_tln.o: src/engine/sched_tln.c src/engine/sched_tln.h src/engine/scheduler.h
	$(CC) $(CFLAGS) -c -o build/sched_tln.o src/engine/sched_tln.c

build/sched_code_ops.o: src/engine/sched_code_ops.c src/engine/sched_code_ops.h src/engine/scheduler.h
	$(CC) $(CFLAGS) -c -o build/sched_code_ops.o src/engine/sched_code_ops.c

build/sched_fission_branch.o: src/engine/sched_fission_branch.c src/engine/sched_fission_branch.h src/engine/scheduler.h
	$(CC) $(CFLAGS) -c -o build/sched_fission_branch.o src/engine/sched_fission_branch.c

build/sched_inductive.o: src/engine/sched_inductive.c src/engine/sched_inductive.h src/engine/scheduler.h
	$(CC) $(CFLAGS) -c -o build/sched_inductive.o src/engine/sched_inductive.c

build/sched_persist.o: src/engine/sched_persist.c src/engine/sched_persist.h src/engine/scheduler.h
	$(CC) $(CFLAGS) -c -o build/sched_persist.o src/engine/sched_persist.c

build/sched_monitor.o: src/engine/sched_monitor.c src/engine/sched_monitor.h src/engine/scheduler.h
	$(CC) $(CFLAGS) -c -o build/sched_monitor.o src/engine/sched_monitor.c

build/sched_idle.o: src/engine/sched_idle.c src/engine/sched_idle.h src/engine/scheduler.h
	$(CC) $(CFLAGS) -c -o build/sched_idle.o src/engine/sched_idle.c

build/beacon.o: src/engine/beacon.c src/engine/beacon.h src/engine/soul_access.h src/learning/pi_seed.h
	$(CC) $(CFLAGS) -c -o build/beacon.o src/engine/beacon.c

build/fractal.o: src/engine/fractal.c src/engine/fractal.h
	$(CC) $(CFLAGS) -c -o build/fractal.o src/engine/fractal.c

build/torkd.o: src/engine/torkd.c src/engine/torkd.h
	$(CC) $(CFLAGS) -c -o build/torkd.o src/engine/torkd.c -lm

# ── Engine link ─────────────────────────────────────────────────────

ENGINE_OBJS = build/tork_engine.o build/monitor.o build/instinct.o build/code_reader.o build/code_modifier.o build/fission.o build/blackboard.o build/self_cal.o build/inductor.o build/persistor.o build/experience.o build/mcts.o build/branch.o build/pattern.o build/replay.o build/observer.o build/snapshot.o build/energy.o build/watcher.o build/query.o build/torkd.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/pi_seed.o build/pi_index.o build/grid_soul_connector.o build/idler.o build/sandbox.o build/agreement.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/tln.o build/code_archive.o build/strict_verifier.o build/scheduler.o build/sched_services.o build/sched_tln.o build/sched_code_ops.o build/sched_fission_branch.o build/sched_inductive.o build/sched_persist.o build/sched_monitor.o build/sched_idle.o build/beacon.o build/fractal.o

build/tork_engine: $(ENGINE_OBJS)
	$(CC) -o build/tork_engine $(ENGINE_OBJS) -lm -lpthread

# ── Grid ──────────────────────────────────────────────────────────

build/tork_grid.o: src/grid/tork_grid.c src/grid/tork_grid.h
	$(CC) $(CFLAGS) -Isrc/grid -c -o build/tork_grid.o src/grid/tork_grid.c

build/grid_main.o: src/grid/grid_main.c src/grid/tork_grid.h
	$(CC) $(CFLAGS) -Isrc/grid -c -o build/grid_main.o src/grid/grid_main.c

build/grid_soul_connector.o: src/grid/grid_soul_connector.c src/grid/grid_soul_connector.h src/grid/tork_grid.h
	$(CC) $(CFLAGS) -Isrc/grid -c -o build/grid_soul_connector.o src/grid/grid_soul_connector.c

build/tork_grid: build/grid_main.o build/tork_grid.o build/grid_soul_connector.o
	$(CC) $(CFLAGS) -Isrc/grid -o build/tork_grid build/grid_main.o build/tork_grid.o build/grid_soul_connector.o -lrt

grid: build/tork_grid
	@echo "网格已编译: ./build/tork_grid [帧数]"

# ── CLI tools ──────────────────────────────────────────────────────

build/tork_ask: src/engine/tork_ask.c build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/torkd.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o
	$(CC) $(CFLAGS) -o build/tork_ask src/engine/tork_ask.c build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/torkd.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o -lm

build/torkd_start: src/engine/torkd_start.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o
	$(CC) $(CFLAGS) -o build/torkd_start src/engine/torkd_start.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o -lm

build/tork: src/engine/tork_cli.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o
	$(CC) $(CFLAGS) -o build/tork src/engine/tork_cli.c build/torkd.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o -lm

torkd: build/torkd_start
	@echo "torkd 已编译: ./build/torkd_start"

# ── Targets ─────────────────────────────────────────────────────────

install: build/tork_engine build/tork_core build/tork_sandbox
	sudo ./src/install/install.sh

run: build/tork_engine build/tork_core
	./build/tork_engine 10

run100: build/tork_engine build/tork_core
	./build/tork_engine 100

probe: build/probe_env
	@echo "=== 环境探测 ==="
	@./build/probe_env | python3 -m json.tool

clean:
	rm -rf build/*.o build/tork_engine build/tork_core build/tork_sandbox build/tork_sandbox_launcher build/tork_grid

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
	@./scripts/tork.sh daemon

stop:
	@./scripts/tork.sh stop

status:
	@./scripts/tork.sh status

# ── AppImage ──────────────────────────────────────────────────────

appimage: all
	@bash scripts/build-installer.sh

# ── Unit Tests ──────────────────────────────────────────────────────

TEST_OBJS = build/sandbox.o build/agreement.o build/code_reader.o build/code_modifier.o build/task.o build/auditor.o build/experience.o build/pattern.o build/pi_seed.o build/pi_index.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/code_archive.o build/strict_verifier.o

build/test_core: tests/test_core.c $(TEST_OBJS)
	$(CC) $(CFLAGS) -o build/test_core tests/test_core.c $(TEST_OBJS) -lm

test: build/test_core
	@./build/test_core
# ── 系统健康验证 ──────────────────────────────────────────────────

verify:
	@bash scripts/verify.sh
