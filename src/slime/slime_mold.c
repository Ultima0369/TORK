#include "slime_mold.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── 初始化 ──────────────────────────────────────────────── */
void slime_init(slime_mold_t *sm) {
    memset(sm, 0, sizeof(slime_mold_t));
    sm->decay_rate = 0.1f;
    sm->growth_rate = 0.3f;
    sm->shrink_rate = 0.1f;
    sm->min_diameter = 0.01f;
    sm->dt = 0.1f;
    sm->tick = 0;
}

/* ── 添加节点 ────────────────────────────────────────────── */
int slime_add_node(slime_mold_t *sm, const char *name,
                   slime_node_type_t type, uint8_t x, uint8_t y) {
    if (sm->node_count >= SLIME_MAX_NODES) return -1;
    if (x >= SLIME_GRID_W || y >= SLIME_GRID_H) return -1;

    int idx = sm->node_count++;
    slime_node_t *n = &sm->nodes[idx];
    strncpy(n->name, name, SLIME_NAME_LEN - 1);
    n->name[SLIME_NAME_LEN - 1] = '\0';
    n->type = type;
    n->x = x;
    n->y = y;
    n->nutrient_in = (type == SLIME_NODE_SOURCE) ? 1.0f : 0.0f;
    n->nutrient_out = (type == SLIME_NODE_SINK) ? 1.0f : 0.0f;
    n->pulse_count = 0;
    n->phase = 0.0f;
    return idx;
}

/* ── 连接节点 ────────────────────────────────────────────── */
int slime_connect(slime_mold_t *sm, uint16_t node_a, uint16_t node_b) {
    if (node_a >= sm->node_count || node_b >= sm->node_count) return -1;
    if (node_a == node_b) return -1;
    if (sm->tube_count >= SLIME_MAX_TUBES) return -1;

    /* 检查是否已连接 */
    for (int i = 0; i < sm->tube_count; i++) {
        if ((sm->tubes[i].node_a == node_a && sm->tubes[i].node_b == node_b) ||
            (sm->tubes[i].node_a == node_b && sm->tubes[i].node_b == node_a))
            return -1;
    }

    int idx = sm->tube_count++;
    slime_tube_t *t = &sm->tubes[idx];
    t->node_a = node_a;
    t->node_b = node_b;
    t->diameter = 1.0f;           /* 初始直径 */
    t->flow = 0.0f;
    t->conductivity = 1.0f;
    t->traffic_count = 0;
    t->last_flow_tick = 0;

    /* 欧氏距离 */
    int dx = sm->nodes[node_a].x - sm->nodes[node_b].x;
    int dy = sm->nodes[node_a].y - sm->nodes[node_b].y;
    t->length = sqrtf((float)(dx * dx + dy * dy));
    if (t->length < 0.1f) t->length = 0.1f;

    return idx;
}

/* ── 全连接 ──────────────────────────────────────────────── */
void slime_connect_all(slime_mold_t *sm) {
    for (int i = 0; i < sm->node_count; i++) {
        for (int j = i + 1; j < sm->node_count; j++) {
            slime_connect(sm, i, j);
        }
    }
}

/* ── 模拟一步 ────────────────────────────────────────────── */
void slime_step(slime_mold_t *sm) {
    sm->tick++;

    /* 1. 源节点分泌营养 */
    for (int i = 0; i < sm->node_count; i++) {
        slime_node_t *n = &sm->nodes[i];
        if (n->type == SLIME_NODE_SOURCE) {
            /* 脉冲调制: 周期性营养分泌 */
            float pulse = sinf(n->phase) * 0.5f + 0.5f;
            n->nutrient_in = 0.5f + pulse * 0.5f;
            n->phase += 0.1f;
            n->pulse_count++;

            int gx = n->x, gy = n->y;
            if (gx < SLIME_GRID_W && gy < SLIME_GRID_H) {
                sm->grid[gy][gx] += n->nutrient_in * sm->dt;
            }
        }
    }

    /* 2. 营养沿管道扩散 (Poiseuille flow: Q = πr⁴ΔP/8μL) */
    /* 简化: flow = conductivity * (浓度差) / length */
    for (int t = 0; t < sm->tube_count; t++) {
        slime_tube_t *tb = &sm->tubes[t];
        if (tb->diameter < sm->min_diameter) continue;

        slime_node_t *na = &sm->nodes[tb->node_a];
        slime_node_t *nb = &sm->nodes[tb->node_b];

        int gxa = na->x, gya = na->y;
        int gxb = nb->x, gyb = nb->y;

        float ca = sm->grid[gya][gxa];
        float cb = sm->grid[gyb][gxb];
        float diff = ca - cb;

        /* 流量: 正比于电导率 × 浓度差 / 长度 */
        tb->flow = tb->conductivity * diff / tb->length;

        /* 传输营养 */
        float transfer = tb->flow * sm->dt;
        sm->grid[gya][gxa] -= transfer;
        sm->grid[gyb][gxb] += transfer;

        if (transfer > 0.001f) {
            tb->traffic_count++;
            tb->last_flow_tick = sm->tick;
        }
    }

    /* 3. 汇节点吸收营养 */
    for (int i = 0; i < sm->node_count; i++) {
        slime_node_t *n = &sm->nodes[i];
        if (n->type == SLIME_NODE_SINK) {
            int gx = n->x, gy = n->y;
            float absorbed = sm->grid[gy][gx] * 0.3f * sm->dt;
            sm->grid[gy][gx] -= absorbed;
            n->nutrient_out += absorbed;
        }
    }

    /* 4. 管道生长/收缩 (正反馈: 流量大 → 直径增大) */
    for (int t = 0; t < sm->tube_count; t++) {
        slime_tube_t *tb = &sm->tubes[t];
        float flow_mag = fabsf(tb->flow);

        /* 生长: 流量越大生长越快 */
        float grow = sm->growth_rate * flow_mag * sm->dt;
        /* 收缩: 无流量时自然萎缩 */
        float shrink = sm->shrink_rate * sm->dt;

        if (flow_mag > 0.01f) {
            tb->diameter += grow;
            tb->conductivity = tb->diameter * tb->diameter; /* r² 近似 */
        } else {
            tb->diameter -= shrink;
            if (tb->diameter < sm->min_diameter) {
                tb->diameter = sm->min_diameter;
                tb->conductivity = 0.001f;
            } else {
                tb->conductivity = tb->diameter * tb->diameter;
            }
        }
    }

    /* 5. 网格营养全局衰减 */
    for (int y = 0; y < SLIME_GRID_H; y++) {
        for (int x = 0; x < SLIME_GRID_W; x++) {
            sm->grid[y][x] *= (1.0f - sm->decay_rate * sm->dt);
            if (sm->grid[y][x] < 0.001f) sm->grid[y][x] = 0.0f;
        }
    }

    /* 6. 保存当前网格作为上一周期 */
    memcpy(sm->grid_prev, sm->grid, sizeof(sm->grid));
}

/* ── 收敛模拟 ────────────────────────────────────────────── */
void slime_converge(slime_mold_t *sm, uint32_t max_steps, float threshold) {
    float prev_total = 0.0f;
    for (uint32_t step = 0; step < max_steps; step++) {
        slime_step(sm);

        float total = slime_total_conductivity(sm);
        if (fabsf(total - prev_total) < threshold && step > 10) break;
        prev_total = total;
    }
}

/* ── 最短路径 (Dijkstra) ────────────────────────────────── */
int slime_find_path(const slime_mold_t *sm,
                    uint16_t from, uint16_t to,
                    slime_path_t *path) {
    if (from >= sm->node_count || to >= sm->node_count) return -1;

    float dist[SLIME_MAX_NODES];
    int prev[SLIME_MAX_NODES];
    uint8_t visited[SLIME_MAX_NODES];
    int count = sm->node_count;

    for (int i = 0; i < count; i++) {
        dist[i] = FLT_MAX;
        prev[i] = -1;
        visited[i] = 0;
    }
    dist[from] = 0;

    for (int i = 0; i < count; i++) {
        /* 找未访问的最小距离节点 */
        float min_d = FLT_MAX;
        int u = -1;
        for (int j = 0; j < count; j++) {
            if (!visited[j] && dist[j] < min_d) {
                min_d = dist[j];
                u = j;
            }
        }
        if (u < 0 || u == (int)to) break;
        visited[u] = 1;

        /* 遍历所有管道 */
        for (int t = 0; t < sm->tube_count; t++) {
            const slime_tube_t *tb = &sm->tubes[t];
            int neighbor = -1;
            if (tb->node_a == (uint16_t)u) neighbor = tb->node_b;
            else if (tb->node_b == (uint16_t)u) neighbor = tb->node_a;
            if (neighbor < 0) continue;

            /* 代价 = 长度 / 直径 (直径越大代价越小) */
            float cost = tb->length / (tb->diameter + 0.1f);
            float nd = dist[u] + cost;
            if (nd < dist[neighbor]) {
                dist[neighbor] = nd;
                prev[neighbor] = u;
            }
        }
    }

    if (dist[to] == FLT_MAX) return -1; /* 不可达 */

    /* 重建路径 */
    uint16_t pbuf[SLIME_MAX_NODES];
    int pcount = 0;
    int cur = to;
    while (cur >= 0 && pcount < SLIME_MAX_NODES) {
        pbuf[pcount++] = (uint16_t)cur;
        cur = prev[cur];
    }

    /* 反转路径 */
    for (int i = 0; i < pcount; i++) {
        path->nodes[i] = pbuf[pcount - 1 - i];
    }
    path->path_len = pcount;
    path->total_cost = dist[to];

    /* 可靠性: 基于路径中最小管道直径 */
    float min_d = FLT_MAX;
    for (int i = 0; i + 1 < pcount; i++) {
        uint16_t a = path->nodes[i], b = path->nodes[i + 1];
        for (int t = 0; t < sm->tube_count; t++) {
            const slime_tube_t *tb = &sm->tubes[t];
            if ((tb->node_a == a && tb->node_b == b) ||
                (tb->node_a == b && tb->node_b == a)) {
                if (tb->diameter < min_d) min_d = tb->diameter;
                break;
            }
        }
    }
    path->reliability = (min_d > 1.0f) ? 1.0f : min_d;

    return 0;
}

/* ── 备选路径 ────────────────────────────────────────────── */
int slime_find_alt_paths(const slime_mold_t *sm,
                         uint16_t from, uint16_t to,
                         slime_path_t *paths, int max_paths) {
    int found = 0;

    /* 简单策略: 依次移除最优路径上的管道再找次优 */
    slime_mold_t temp;
    memcpy(&temp, sm, sizeof(slime_mold_t));

    for (int p = 0; p < max_paths; p++) {
        if (slime_find_path(&temp, from, to, &paths[p]) != 0) break;

        /* 移除这条路径上的所有管道 (找下一条备用路) */
        for (int i = 0; i + 1 < paths[p].path_len; i++) {
            uint16_t a = paths[p].nodes[i], b = paths[p].nodes[i + 1];
            for (int t = 0; t < temp.tube_count; t++) {
                if ((temp.tubes[t].node_a == a && temp.tubes[t].node_b == b) ||
                    (temp.tubes[t].node_a == b && temp.tubes[t].node_b == a)) {
                    temp.tubes[t].diameter = 0.001f;
                    temp.tubes[t].conductivity = 0.0f;
                    break;
                }
            }
        }
        found++;
    }

    return found;
}

/* ── 脉冲 ────────────────────────────────────────────────── */
void slime_pulse(slime_mold_t *sm, uint16_t node_idx, float amount) {
    if (node_idx >= sm->node_count) return;
    slime_node_t *n = &sm->nodes[node_idx];
    int gx = n->x, gy = n->y;
    if (gx < SLIME_GRID_W && gy < SLIME_GRID_H) {
        sm->grid[gy][gx] += amount;
        n->pulse_count++;
    }
}

/* ── 设置需求 ────────────────────────────────────────────── */
void slime_set_demand(slime_mold_t *sm, uint16_t node_idx, float demand) {
    if (node_idx >= sm->node_count) return;
    sm->nodes[node_idx].nutrient_in = demand > 0.0f ? demand : 0.0f;
}

/* ── 统计 ────────────────────────────────────────────────── */
uint16_t slime_node_count(const slime_mold_t *sm) { return sm->node_count; }
uint16_t slime_tube_count(const slime_mold_t *sm) { return sm->tube_count; }

float slime_total_conductivity(const slime_mold_t *sm) {
    float total = 0.0f;
    for (int t = 0; t < sm->tube_count; t++) {
        total += sm->tubes[t].conductivity;
    }
    return total;
}

/* ── 打印网络 ────────────────────────────────────────────── */
void slime_print_network(const slime_mold_t *sm) {
    printf("Slime Mold Network (tick %lu)\n", (unsigned long)sm->tick);
    printf("  Nodes: %u  Tubes: %u\n", sm->node_count, sm->tube_count);

    for (int i = 0; i < sm->node_count; i++) {
        const slime_node_t *n = &sm->nodes[i];
        printf("  N%02d [%s] (%d,%d) type=%d in=%.2f out=%.2f pulses=%u\n",
               i, n->name, n->x, n->y, n->type,
               n->nutrient_in, n->nutrient_out, n->pulse_count);
    }

    /* 只打印直径 > 0.1 的活跃管道 */
    for (int t = 0; t < sm->tube_count; t++) {
        const slime_tube_t *tb = &sm->tubes[t];
        if (tb->diameter < 0.1f) continue;
        printf("  T%02d: N%02d--N%02d d=%.2f cond=%.2f flow=%.3f traffic=%u\n",
               t, tb->node_a, tb->node_b, tb->diameter,
               tb->conductivity, tb->flow, tb->traffic_count);
    }
}

/* ── DOT 导出 ────────────────────────────────────────────── */
void slime_export_dot(const slime_mold_t *sm, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "graph slime_mold {\n");
    fprintf(f, "  rankdir=LR;\n");
    fprintf(f, "  node [style=filled];\n");

    for (int i = 0; i < sm->node_count; i++) {
        const slime_node_t *n = &sm->nodes[i];
        const char *color = "white";
        if (n->type == SLIME_NODE_SOURCE) color = "lightgreen";
        else if (n->type == SLIME_NODE_SINK) color = "lightcoral";
        else if (n->type == SLIME_NODE_BLOCK) color = "gray";
        fprintf(f, "  N%d [label=\"%s\" fillcolor=%s];\n", i, n->name, color);
    }

    for (int t = 0; t < sm->tube_count; t++) {
        const slime_tube_t *tb = &sm->tubes[t];
        if (tb->diameter < 0.05f) continue;
        int penwidth = (int)(tb->diameter * 2);
        if (penwidth < 1) penwidth = 1;
        if (penwidth > 10) penwidth = 10;
        const char *color = tb->flow > 0 ? "blue" : "gray70";
        fprintf(f, "  N%d -- N%d [penwidth=%d color=%s label=\"%.2f\"];\n",
                tb->node_a, tb->node_b, penwidth, color, tb->flow);
    }

    fprintf(f, "}\n");
    fclose(f);
}

/* ════════════════════════════════════════════════════════════ */
/* 工作流编排器实现                                            */
/* ════════════════════════════════════════════════════════════ */

void slime_wf_init(slime_workflow_t *wf, slime_mold_t *mold) {
    memset(wf, 0, sizeof(slime_workflow_t));
    wf->mold = mold;
    wf->wf_count = 0;
    wf->active_path_len = 0;
}

int slime_wf_register(slime_workflow_t *wf, const char *name,
                      slime_hook_type_t hook, float compute, float mem,
                      void (*exec)(void*)) {
    if (wf->wf_count >= SLIME_MAX_WF_NODES) return -1;
    int idx = wf->wf_count++;

    slime_workflow_node_t *wn = &wf->wf_nodes[idx];
    strncpy(wn->name, name, SLIME_NAME_LEN - 1);
    wn->name[SLIME_NAME_LEN - 1] = '\0';
    wn->hook_type = hook;
    wn->compute_cost = compute;
    wn->memory_cost = mem;
    wn->exec_fn = exec;

    /* 在黏菌网络中创建一个对应节点 */
    slime_node_type_t sn_type = SLIME_NODE_WAY;
    if (hook == SLIME_HOOK_INPUT) sn_type = SLIME_NODE_SOURCE;
    else if (hook == SLIME_HOOK_OUTPUT) sn_type = SLIME_NODE_SINK;

    /* 节点位置: 自动布局 - 按注册顺序排列 */
    uint8_t x = (uint8_t)((idx % 8) * 8);
    uint8_t y = (uint8_t)((idx / 8) * 8);

    wn->node_idx = slime_add_node(wf->mold, name, sn_type, x, y);

    return idx;
}

int slime_wf_connect(slime_workflow_t *wf, uint16_t a, uint16_t b) {
    if (a >= wf->wf_count || b >= wf->wf_count) return -1;
    uint16_t na = wf->wf_nodes[a].node_idx;
    uint16_t nb = wf->wf_nodes[b].node_idx;
    return slime_connect(wf->mold, na, nb);
}

int slime_wf_run(slime_workflow_t *wf, uint16_t start, uint16_t end) {
    if (start >= wf->wf_count || end >= wf->wf_count) return -1;

    uint16_t na = wf->wf_nodes[start].node_idx;
    uint16_t nb = wf->wf_nodes[end].node_idx;

    /* 给源节点一个营养脉冲启动流程 */
    slime_pulse(wf->mold, na, 10.0f);

    /* 模拟几步让黏菌生长 */
    slime_converge(wf->mold, 100, 0.01f);

    /* 找最优路径 */
    slime_path_t path;
    if (slime_find_path(wf->mold, na, nb, &path) != 0) return -1;

    wf->active_path_len = path.path_len;
    for (int i = 0; i < path.path_len; i++) {
        wf->active_path[i] = path.nodes[i];
    }

    /* 沿路径执行节点 */
    for (int i = 0; i < path.path_len; i++) {
        uint16_t nidx = path.nodes[i];
        for (int w = 0; w < wf->wf_count; w++) {
            if (wf->wf_nodes[w].node_idx == nidx) {
                if (wf->wf_nodes[w].exec_fn) {
                    wf->wf_nodes[w].exec_fn(NULL);
                }
                break;
            }
        }
    }

    return 0;
}
