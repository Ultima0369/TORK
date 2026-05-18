# Auto-generated from Makefile
# Section: Learning modules

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
