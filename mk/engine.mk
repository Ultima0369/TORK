# Auto-generated from Makefile
# Section: C engine

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

build/torkd.o: src/engine/torkd.c src/engine/torkd.h src/engine/soul_access.h src/engine/scheduler.h src/learning/mentor.h
	$(CC) $(CFLAGS) -c -o build/torkd.o src/engine/torkd.c

ENGINE_OBJS = build/tork_engine.o build/monitor.o build/instinct.o build/code_reader.o build/code_modifier.o build/fission.o build/blackboard.o build/self_cal.o build/inductor.o build/persistor.o build/experience.o build/mcts.o build/branch.o build/pattern.o build/replay.o build/observer.o build/snapshot.o build/energy.o build/watcher.o build/query.o build/torkd.o build/self_build.o build/mutation_guide.o build/self_tune.o build/mentor.o build/distributed.o build/pi_seed.o build/pi_index.o build/grid_soul_connector.o build/idler.o build/sandbox.o build/agreement.o build/growth_node.o build/task.o build/auditor.o build/dispatch.o build/codegen.o build/tln.o build/code_archive.o build/strict_verifier.o build/scheduler.o build/sched_services.o build/sched_tln.o build/sched_code_ops.o build/sched_fission_branch.o build/sched_inductive.o build/sched_persist.o build/sched_monitor.o build/sched_idle.o build/beacon.o build/swarm.o build/visual.o build/fractal.o build/sp_bridge.o build/bridge_integration.o build/mcts_persist.o build/rollback.o
build/beacon.o: src/engine/beacon.c src/engine/beacon.h src/engine/soul_access.h src/learning/pi_seed.h
	$(CC) $(CFLAGS) -c -o build/beacon.o src/engine/beacon.c

build/swarm.o: src/engine/swarm.c src/engine/swarm.h src/engine/beacon.h src/learning/distributed.h
	$(CC) $(CFLAGS) -c -o build/swarm.o src/engine/swarm.c

build/visual.o: src/engine/visual.c src/engine/visual.h
	$(CC) $(CFLAGS) -c -o build/visual.o src/engine/visual.c

build/rollback.o: src/rollback/rollback.c src/rollback/rollback.h
	$(CC) $(CFLAGS) -Isrc/rollback -c -o build/rollback.o src/rollback/rollback.c

build/sp_bridge.o: src/bridge/sp_bridge.c src/bridge/sp_bridge.h
	$(CC) $(CFLAGS) -c -o build/sp_bridge.o src/bridge/sp_bridge.c

build/bridge_integration.o: src/bridge/bridge_integration.c src/bridge/bridge_integration.h
	$(CC) $(CFLAGS) -c -o build/bridge_integration.o src/bridge/bridge_integration.c

build/fractal.o: src/engine/fractal.c src/engine/fractal.h
	$(CC) $(CFLAGS) -c -o build/fractal.o src/engine/fractal.c


build/tork_engine: $(ENGINE_OBJS)
	$(CC) -o build/tork_engine $(ENGINE_OBJS) -lm -lpthread

# ── Crypto ────────────────────────────────────────────────
build/tork_sha256.o: src/crypto/tork_sha256.c src/crypto/tork_sha256.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/tork_cipher.o: src/crypto/tork_cipher.c src/crypto/tork_cipher.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/tork_watchdog.o: src/engine/tork_watchdog.c src/engine/tork_watchdog.h
	$(CC) $(CFLAGS) -c -o $@ $<
# ── Edge P1 ────────────────────────────────────────────────
build/edge_time.o: src/edge/edge_time.c src/edge/edge_sensor.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/temp_fallback.o: src/edge/temp_fallback.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Engine P2 ────────────────────────────────────────────────
build/tork_log.o: src/engine/tork_log.c src/engine/tork_log.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/tork_jsmn.o: src/engine/tork_jsmn.c src/engine/tork_jsmn.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/tork_pbft.o: src/mesh/tork_pbft.c src/mesh/tork_pbft.h
	$(CC) $(CFLAGS) -Isrc/mesh -c -o $@ $<

# ── Test framework ──────────────────────────────────────────
build/unity.o: tests/unity.c tests/unity.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: build/unity.o
	$(CC) $(CFLAGS) -Isrc/mesh -o build/test_pbft tests/unity.c src/mesh/tork_pbft.c -DTEST_BUILD
	@echo "Running PBFT consensus tests..."
	./build/test_pbft
	@echo ""
	@echo "All P2 unit tests completed."
