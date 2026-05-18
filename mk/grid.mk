# Auto-generated from Makefile
# Section: Grid

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


