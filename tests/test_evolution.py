"""
TORK 云端进化引擎 v3.0 — 单元测试套件

覆盖:
  strategy_generator: FailurePattern, StrategyGenerator, FailureAnalyzer
  code_bridge:        ParamModulator, MarkerInjector, MCTSBridge, CodeModifierBridge, MutationExecutor
  validator:          FitnessCalculator, ValidatorPipeline
  evolution:          TorkEvolution (集成测试)

运行:
  python3 tests/test_evolution.py
"""
from __future__ import annotations

import sys, os, json, math, tempfile, shutil, inspect
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# ── 测试框架 ──────────────────────────────────────────────────────
g_pass = 0
g_fail = 0

def test(name: str, cond: bool, detail: str = "") -> None:
    global g_pass, g_fail
    if cond:
        g_pass += 1
        print(f"  ✓ {name}")
    else:
        g_fail += 1
        msg = f"  ✗ {name}"
        if detail:
            msg += f"\n      {detail}"
        print(msg)

def eq(a: object, b: object, ctx: str = "") -> None:
    test(f"{ctx}: {a} == {b}", a == b,
         f"got {repr(a)}, expected {repr(b)}")

def ne(a: object, b: object, ctx: str = "") -> None:
    test(f"{ctx}: {a} != {b}", a != b,
         f"got {repr(a)}, should not be {repr(b)}")

def ae(a: float, b: float, eps: float = 1e-6, ctx: str = "") -> None:
    test(f"{ctx}: {a} ≈ {b}", abs(a - b) < eps,
         f"got {a}, expected {b}, diff {abs(a-b)}")

def true(cond: bool, ctx: str = "") -> None:
    test(ctx, cond, f"expected True, got {cond}")

def false(cond: bool, ctx: str = "") -> None:
    test(f"NOT: {ctx}", not cond, f"expected False, got {cond}")

def in_(item: object, container: object, ctx: str = "") -> None:
    test(f"{ctx}: {item} in container", item in container,
         f"'{item}' not found")

class TmpFile:
    """临时文件上下文管理器"""
    def __init__(self, content: str, suffix: str = ".c") -> None:
        self.fd, self.path = tempfile.mkstemp(suffix=suffix, text=True)
        os.close(self.fd)
        with open(self.path, "w") as f:
            f.write(content)
    def __enter__(self) -> str:
        return self.path
    def __exit__(self, *args: object) -> None:
        os.unlink(self.path)

# ══════════════════════════════════════════════════════════════════
#  ║  FailurePattern
# ══════════════════════════════════════════════════════════════════

def test_failure_pattern_exact_match() -> None:
    from cloud.strategy_generator import FailurePattern
    fp = FailurePattern("id1", "desc", "mod_param",
                        {"param": "fear_weight"}, 30)
    true(fp.matches("mod_param", {"param": "fear_weight"}), "精确匹配")
    false(fp.matches("mod_param", {"param": "curiosity"}), "不同值不匹配")
    false(fp.matches("inject", {"param": "fear_weight"}), "不同类型不匹配")

def test_failure_pattern_wildcard() -> None:
    from cloud.strategy_generator import FailurePattern
    fp = FailurePattern("id2", "wildcard", "mod_param",
                        {"param": "*"}, 30)
    true(fp.matches("mod_param", {"param": "fear"}),      "通配:* → fear")
    true(fp.matches("mod_param", {"param": "anything"}),   "通配:* → anything")
    false(fp.matches("mod_param", {}),                     "无param键不匹配")
    false(fp.matches("mod_param", {"other": "x"}),         "其他键不匹配")

def test_failure_pattern_any_action() -> None:
    from cloud.strategy_generator import FailurePattern
    fp = FailurePattern("id3", "any", "mod_param")
    true(fp.matches("mod_param", {"x": 1}), "同类型匹配")
    false(fp.matches("other", {"x": 1}), "不同类型不匹配")

# ══════════════════════════════════════════════════════════════════
#  ║  StrategyGenerator
# ══════════════════════════════════════════════════════════════════

def test_known_params() -> None:
    from cloud.strategy_generator import KNOWN_PARAMS
    in_("fear_weight", KNOWN_PARAMS)
    in_("curiosity_weight", KNOWN_PARAMS)
    in_("desire_weight", KNOWN_PARAMS)
    true(len(KNOWN_PARAMS) >= 5)

def test_strategy_generator_init() -> None:
    from cloud.strategy_generator import StrategyGenerator
    sg = StrategyGenerator()
    true(sg is not None, "StrategyGenerator() 创建成功")

def test_strategy_generator_generate_empty() -> None:
    """无历史时返回兜底策略列表"""
    from cloud.strategy_generator import StrategyGenerator
    sg = StrategyGenerator()
    result = sg.generate({}, [])
    true(isinstance(result, list), "应返回列表")
    true(len(result) > 0, "至少有一个兜底策略")
    in_("action_type", result[0])
    in_("action_params", result[0])

def test_failure_analyzer_analyze() -> None:
    from cloud.strategy_generator import FailureAnalyzer
    fa = FailureAnalyzer()
    record = {"mutagen": "curiosity+15", "survived": False}
    result = fa.analyze(record, "compilation error")
    true(isinstance(result, dict), "应返回 dict")
    in_("mutagen", result)
    in_("result", result)
    in_("patterns", result)

# ══════════════════════════════════════════════════════════════════
#  ║  ParamModulator
# ══════════════════════════════════════════════════════════════════

def test_param_modulator_modulate() -> None:
    from cloud.code_bridge import ParamModulator
    pm = ParamModulator()
    with TmpFile("// fear_weight = 50\n") as f:
        result = pm.modulate(f, "fear_weight", 15, "struct_field")
        true(isinstance(result, bool), "返回 bool")

def test_param_modulator_invalid_file() -> None:
    from cloud.code_bridge import ParamModulator
    pm = ParamModulator()
    false(pm.modulate("/nonexistent/path.c", "fear_weight", 5))

# ══════════════════════════════════════════════════════════════════
#  ║  MarkerInjector
# ══════════════════════════════════════════════════════════════════

def test_marker_injector_inject() -> None:
    from cloud.code_bridge import MarkerInjector
    mi = MarkerInjector()
    content = "int x;\n// TORK_EVOLVE: test_marker\nint y;\n"
    with TmpFile(content) as f:
        result = mi.inject(f, "test_marker", "z = 1;")
        true(result, "注入应成功")
        with open(f) as f2:
            new_content = f2.read()
        in_("z = 1;", new_content, "注入代码应出现")
        in_("evo_injected", new_content, "防护标记应出现")

def test_marker_injector_no_marker() -> None:
    from cloud.code_bridge import MarkerInjector
    mi = MarkerInjector()
    with TmpFile("int x;\nint y;\n") as f:
        false(mi.inject(f, "nonexistent", "z = 1;"), "无标记应返回 False")

def test_marker_injector_already_injected() -> None:
    from cloud.code_bridge import MarkerInjector
    mi = MarkerInjector()
    content = "int x;\n// TORK_EVOLVE: test_m2\n/* evo_injected */\nint y;\n"
    with TmpFile(content) as f:
        false(mi.inject(f, "test_m2", "z = 1;"), "已注入不应重复")

def test_marker_injector_protocols() -> None:
    from cloud.code_bridge import MarkerInjector
    mi = MarkerInjector()
    content = "int x;\n// TORK_EVOLVE: test_m3\nint y;\n"
    with TmpFile(content) as f:
        true(mi.inject(f, "test_m3", "z = 1;", "INSERT_AFTER"), "INSERT_AFTER")
        with open(f) as f2:
            c = f2.read()
        in_("z = 1;", c, "INSERT_AFTER 应插入")

# ══════════════════════════════════════════════════════════════════
#  ║  MCTSBridge
# ══════════════════════════════════════════════════════════════════

def test_mcts_bridge_write_file() -> None:
    from cloud.code_bridge import MCTSBridge
    import cloud.code_bridge as cb
    old_base = cb.BASE
    cb.BASE = tempfile.mkdtemp()
    try:
        bridge = MCTSBridge()
        result = bridge.send_mcts_action(3, 42)
        true(result, "send 应成功")
        fpath = os.path.join(cb.BASE, "persist", "cloud_mcts_action.json")
        true(os.path.exists(fpath), "文件应存在")
        with open(fpath) as f:
            data = json.load(f)
        eq(data["mcts_type"], 3, "mcts_type")
        eq(data["param"], 42, "param")
        eq(data["action_name"], "TRY_MODIFY", "action_name")
    finally:
        shutil.rmtree(cb.BASE, ignore_errors=True)
        cb.BASE = old_base

def test_mcts_bridge_action_names() -> None:
    from cloud.code_bridge import MCTSBridge
    bridge = MCTSBridge()
    eq(bridge.ACTION_NAMES[0], "ADJUST_FEAR")
    eq(bridge.ACTION_NAMES[1], "ADJUST_CURIOSITY")
    eq(bridge.ACTION_NAMES[7], "MOD_REPLACE_OP")
    true(len(bridge.ACTION_NAMES) >= 10)

# ══════════════════════════════════════════════════════════════════
#  ║  CodeModifierBridge
# ══════════════════════════════════════════════════════════════════

def test_replace_operand() -> None:
    from cloud.code_bridge import CodeModifierBridge
    cmb = CodeModifierBridge()
    asm = "\t.text\ntest_fn:\n\tmovq %rax, %rbx\n\tret\n"
    with TmpFile(asm, ".s") as f:
        result = cmb.replace_operand(f, "test_fn", "%rax", "%rcx")
        true(result, "替换应成功")
        with open(f) as f2:
            c = f2.read()
        in_("%rcx", c, "新操作数出现")
        in_("%rbx", c, "无关操作数保留")

def test_replace_operand_no_match() -> None:
    from cloud.code_bridge import CodeModifierBridge
    cmb = CodeModifierBridge()
    asm = "\t.text\ntest_fn:\n\tmovq %rdx, %rbx\n\tret\n"
    with TmpFile(asm, ".s") as f:
        false(cmb.replace_operand(f, "test_fn", "%rax", "%rcx"), "无匹配返回 False")

def test_delete_nop() -> None:
    from cloud.code_bridge import CodeModifierBridge
    cmb = CodeModifierBridge()
    asm = "\t.text\ntest_fn:\n\tnop\n\tmovq $1, %rax\n\tnopw\n\tret\n"
    with TmpFile(asm, ".s") as f:
        result = cmb.delete_nop(f, "test_fn")
        true(result, "删除 NOP 应成功")
        with open(f) as f2:
            c = f2.read()
        in_("movq", c, "保留 movq")
        in_("ret", c, "保留 ret")

def test_delete_dead() -> None:
    """P0修复验证: 删除 ret 后死代码"""
    from cloud.code_bridge import CodeModifierBridge
    cmb = CodeModifierBridge()
    asm = ("\t.text\ntest_fn:\n"
           "\tmovq $1, %rax\n"
           "\tret\n"
           "\tnop\n"
           "\tmovq $2, %rbx\n")
    with TmpFile(asm, ".s") as f:
        result = cmb.delete_dead(f, "test_fn")
        true(result, "删除死代码应成功")
        with open(f) as f2:
            c = f2.read()
        in_("movq $1", c, "ret 前保留")
        in_("ret", c, "ret 保留")
        true("movq $2" not in c, f"死代码应删除: {c}")

def test_delete_dead_no_ret() -> None:
    """无 ret 函数应无删除"""
    from cloud.code_bridge import CodeModifierBridge
    cmb = CodeModifierBridge()
    asm = "\t.text\nloop_fn:\n\tjmp loop_fn\n"
    with TmpFile(asm, ".s") as f:
        false(cmb.delete_dead(f, "loop_fn"), "无 ret 返回 False")

# ══════════════════════════════════════════════════════════════════
#  ║  MutationExecutor
# ══════════════════════════════════════════════════════════════════

def test_mutation_executor_invalid_strategy() -> None:
    from cloud.code_bridge import MutationExecutor
    me = MutationExecutor()
    # 无效策略 → 应返回 False 不抛异常
    false(me.execute({"action_type": "nonexistent"}), "无效策略返回 False")

def test_mutation_executor_empty_strategy() -> None:
    from cloud.code_bridge import MutationExecutor
    me = MutationExecutor()
    false(me.execute({}), "空策略返回 False")

# ══════════════════════════════════════════════════════════════════
#  ║  FitnessCalculator
# ══════════════════════════════════════════════════════════════════

def test_fitness_normalized() -> None:
    from cloud.validator import FitnessCalculator
    fc = FitnessCalculator()
    s = fc.compute_normalized_fitness(100, 1, 0.8)
    true(0.0 <= s <= 1.0, f"分数应在 [0,1]: {s}")

def test_fitness_compile_failure() -> None:
    from cloud.validator import FitnessCalculator
    fc = FitnessCalculator()
    s = fc.compute_normalized_fitness(0, 0, 0.0)
    eq(s, 0.0, "编译失败分数为0")

def test_fitness_runtime_matters() -> None:
    from cloud.validator import FitnessCalculator
    fc = FitnessCalculator()
    high = fc.compute_normalized_fitness(100, 1, 0.9)
    low = fc.compute_normalized_fitness(100, 1, 0.1)
    true(high > low, f"高运行时 > 低运行时: {high} > {low}")

def test_fitness_history() -> None:
    from cloud.validator import FitnessCalculator
    fc = FitnessCalculator()
    entry = {"compile_ok": True, "survival_ticks": 200, "runtime_health": 0.7}
    s = fc.compute_from_history(entry, 0.7)
    true(0.0 <= s <= 1.0, f"历史分数应在 [0,1]: {s}")

# ══════════════════════════════════════════════════════════════════
#  ║  ValidatorPipeline
# ══════════════════════════════════════════════════════════════════

def test_validator_returns_dict() -> None:
    from cloud.validator import ValidatorPipeline
    vp = ValidatorPipeline()
    result = vp.validate(runtime=False, compile_timeout=5)
    true(isinstance(result, dict), "返回 dict")
    in_("compile_ok", result)

# ══════════════════════════════════════════════════════════════════
#  ║  Evolution 集成
# ══════════════════════════════════════════════════════════════════

def test_evolution_init() -> None:
    from cloud.evolution import TorkEvolution
    evo = TorkEvolution()
    in_("generation", evo.history, "history 应有 generation 字段")
    eq(evo.history["generation"], 0, "初始代数0")

def test_evolution_has_fitness_history() -> None:
    from cloud.evolution import TorkEvolution
    evo = TorkEvolution()
    in_("history", dir(evo))
    in_("fitness", dir(evo))

def test_evolution_roundtrip() -> None:
    from cloud.evolution import TorkEvolution
    import cloud.evolution as ev
    old_dir = ev.WORK_DIR if hasattr(ev, 'WORK_DIR') else None
    tmpdir = tempfile.mkdtemp()
    try:
        if hasattr(ev, 'WORK_DIR'):
            orig = ev.WORK_DIR
            ev.WORK_DIR = tmpdir
        evo = TorkEvolution()
        evo.history["generation"] = 42
        evo._save_history()
        evo2 = TorkEvolution()
        eq(evo2.history["generation"], 42, "roundtrip 恢复代数")
        if hasattr(ev, 'WORK_DIR'):
            ev.WORK_DIR = orig
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

# ══════════════════════════════════════════════════════════════════
#  ║  边界与异常
# ══════════════════════════════════════════════════════════════════

def test_code_modifier_empty_file() -> None:
    from cloud.code_bridge import CodeModifierBridge
    cmb = CodeModifierBridge()
    with TmpFile("", ".s") as f:
        false(cmb.replace_operand(f, "fn", "%rax", "%rbx"), "空文件替换")
        false(cmb.delete_nop(f, "fn"), "空文件删除 NOP")
        false(cmb.delete_dead(f, "fn"), "空文件删除死代码")

def test_marker_injector_empty_marker() -> None:
    from cloud.code_bridge import MarkerInjector
    mi = MarkerInjector()
    with TmpFile("int x;\n") as f:
        false(mi.inject(f, "", "z;"), "空标记")
        false(mi.inject(f, "  ", "z;"), "空白标记")

def test_param_modulator_extreme_delta() -> None:
    from cloud.code_bridge import ParamModulator
    pm = ParamModulator()
    with TmpFile("// fear_weight = 50\n") as f:
        result = pm.modulate(f, "fear_weight", -9999, "struct_field")
        true(isinstance(result, bool), "极端 delta 不抛异常")

def test_mcts_bridge_invalid_action() -> None:
    from cloud.code_bridge import MCTSBridge
    bridge = MCTSBridge()
    import cloud.code_bridge as cb
    old_base = cb.BASE
    cb.BASE = tempfile.mkdtemp()
    try:
        result = bridge.send_mcts_action(999, 0)
        true(result, "无效 action type 仍应写入")
    finally:
        shutil.rmtree(cb.BASE, ignore_errors=True)
        cb.BASE = old_base

# ══════════════════════════════════════════════════════════════════
#  ║  运行
# ══════════════════════════════════════════════════════════════════

def main() -> None:
    global g_pass, g_fail

    print("═" * 56)
    print("  TORK 进化引擎 v3.0 — 单元测试")
    print(f"  Python {sys.version.split()[0]}")
    print("═" * 56)

    suites = [
        ("FailurePattern", [
            test_failure_pattern_exact_match,
            test_failure_pattern_wildcard,
            test_failure_pattern_any_action,
        ]),
        ("StrategyGenerator", [
            test_known_params,
            test_strategy_generator_init,
            test_strategy_generator_generate_empty,
            test_failure_analyzer_analyze,
        ]),
        ("ParamModulator", [
            test_param_modulator_modulate,
            test_param_modulator_invalid_file,
        ]),
        ("MarkerInjector", [
            test_marker_injector_inject,
            test_marker_injector_no_marker,
            test_marker_injector_already_injected,
            test_marker_injector_protocols,
        ]),
        ("MCTSBridge", [
            test_mcts_bridge_write_file,
            test_mcts_bridge_action_names,
        ]),
        ("CodeModifierBridge", [
            test_replace_operand,
            test_replace_operand_no_match,
            test_delete_nop,
            test_delete_dead,
            test_delete_dead_no_ret,
        ]),
        ("MutationExecutor", [
            test_mutation_executor_invalid_strategy,
            test_mutation_executor_empty_strategy,
        ]),
        ("FitnessCalculator", [
            test_fitness_normalized,
            test_fitness_compile_failure,
            test_fitness_runtime_matters,
            test_fitness_history,
        ]),
        ("ValidatorPipeline", [
            test_validator_returns_dict,
        ]),
        ("Evolution Integration", [
            test_evolution_init,
            test_evolution_has_fitness_history,
        ]),
        ("Boundary & Exception", [
            test_code_modifier_empty_file,
            test_marker_injector_empty_marker,
            test_param_modulator_extreme_delta,
            test_mcts_bridge_invalid_action,
        ]),
    ]

    for suite_name, tests_list in suites:
        print(f"\n  ── {suite_name} ({len(tests_list)} tests) ──")
        for t in tests_list:
            try:
                t()
            except Exception as e:
                g_fail += 1
                import traceback
                tb = traceback.format_exc().split('\n')[-3:-1]
                print(f"  ✗ {t.__name__} [UNHANDLED: {e}]")
                for line in tb:
                    if line.strip():
                        print(f"      {line.strip()}")

    print(f"\n{'═' * 56}")
    total = g_pass + g_fail
    print(f"  合计: {total} 项测试")
    print(f"  通过: {g_pass}")
    print(f"  失败: {g_fail}")
    if g_fail == 0:
        print(f"  >>> 全部通过 <<<")
    else:
        print(f"  >>> {g_fail} 项失败 <<<")
    print(f"{'═' * 56}")

    sys.exit(0 if g_fail == 0 else 1)

if __name__ == "__main__":
    main()
