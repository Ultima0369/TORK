# Auto-generated from Makefile
# Sections: CLI tools, Targets, Dashboard (Python), 完整启动, AppImage, Unit Tests, 系统健康验证

# ── CLI tools ──────────────────────────────────────────────────────

build/tork_ask: src/engine/tork_ask.c build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/torkd.o build/scheduler.o build/sched_services.o build/sched_tln.o build/sched_code_ops.o build/sched_fission_branch.o build/sched_inductive.o build/sched_persist.o build/sched_monitor.o build/sched_idle.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/growth_node.o build/idler.o build/code_reader.o build/code_modifier.o
	$(CC) $(CFLAGS) -o build/tork_ask src/engine/tork_ask.c build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/torkd.o build/scheduler.o build/sched_services.o build/sched_tln.o build/sched_code_ops.o build/sched_fission_branch.o build/sched_inductive.o build/sched_persist.o build/sched_monitor.o build/sched_idle.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/growth_node.o build/idler.o build/code_reader.o build/code_modifier.o -lm

build/torkd_start: src/engine/torkd_start.c build/torkd.o build/scheduler.o build/sched_services.o build/sched_tln.o build/sched_code_ops.o build/sched_fission_branch.o build/sched_inductive.o build/sched_persist.o build/sched_monitor.o build/sched_idle.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/growth_node.o build/idler.o build/code_reader.o build/code_modifier.o
	$(CC) $(CFLAGS) -o build/torkd_start src/engine/torkd_start.c build/torkd.o build/scheduler.o build/sched_services.o build/sched_tln.o build/sched_code_ops.o build/sched_fission_branch.o build/sched_inductive.o build/sched_persist.o build/sched_monitor.o build/sched_idle.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/growth_node.o build/idler.o build/code_reader.o build/code_modifier.o -lm

build/tork: src/engine/tork_cli.c build/torkd.o build/scheduler.o build/sched_services.o build/sched_tln.o build/sched_code_ops.o build/sched_fission_branch.o build/sched_inductive.o build/sched_persist.o build/sched_monitor.o build/sched_idle.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/growth_node.o build/idler.o build/code_reader.o build/code_modifier.o
	$(CC) $(CFLAGS) -o build/tork src/engine/tork_cli.c build/torkd.o build/scheduler.o build/sched_services.o build/sched_tln.o build/sched_code_ops.o build/sched_fission_branch.o build/sched_inductive.o build/sched_persist.o build/sched_monitor.o build/sched_idle.o build/query.o build/watcher.o build/snapshot.o build/observer.o build/energy.o build/experience.o build/branch.o build/pattern.o build/pi_seed.o build/pi_index.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/mcts.o build/replay.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/fission.o build/blackboard.o build/sandbox.o build/agreement.o build/growth_node.o build/idler.o build/code_reader.o build/code_modifier.o -lm

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

build/test_blackboard: tests/test_blackboard.c build/blackboard.o
	$(CC) $(CFLAGS) -o build/test_blackboard tests/test_blackboard.c build/blackboard.o -lm

build/stub_self_tune.o: tests/stub_self_tune.c
	$(CC) $(CFLAGS) -c -o build/stub_self_tune.o tests/stub_self_tune.c

build/test_instinct: tests/test_instinct.c build/instinct.o build/stub_self_tune.o
	$(CC) $(CFLAGS) -o build/test_instinct tests/test_instinct.c build/instinct.o build/stub_self_tune.o -lm

build/test_tln: tests/test_tln.c build/tln.o build/pi_seed.o
	$(CC) $(CFLAGS) -o build/test_tln tests/test_tln.c build/tln.o build/pi_seed.o -lm

test: build/test_core build/test_blackboard build/test_instinct build/test_tln
	@./build/test_core
	@./build/test_blackboard
	@./build/test_instinct
	@./build/test_tln

# ── 系统健康验证 ──────────────────────────────────────────────────

verify:
	@bash scripts/verify.sh


