"""The static internal-RAM gate the firmware build runs (esp32/scripts/static_ram.py): run with
uv run --with pytest pytest esp32/test/scripts
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from static_ram import BUDGET, over_budget, usage  # noqa: E402

# Section sizes of an esp32-s3-n8r2 build's ELF (2026-10-22): flash, PSRAM and the linker's placeholders do not count.
SECTIONS = {".iram0.vectors": 1028, ".iram0.text": 85507, ".iram0.text_end": 249, ".iram0.data": 0, ".iram0.bss": 0,
            ".dram0.dummy": 70400, ".dram0.data": 33896, ".dram0.bss": 106464, ".noinit": 0, ".dram0.heap_start": 0,
            ".flash.text": 1467480, ".flash.rodata": 880844, ".flash.rodata_noload": 12575, ".ext_ram.dummy": 2424800}


def test_usage_counts_the_static_data_and_the_iram_sections_only():
    assert usage(SECTIONS) == {"dram_bytes": 140360, "iram_bytes": 86784}


def test_a_20_kb_static_buffer_fails_the_gate():
    budget = {"dram_bytes": 141000, "iram_bytes": 87000}
    assert over_budget(usage(SECTIONS), budget) == []
    grown = {**SECTIONS, ".dram0.bss": SECTIONS[".dram0.bss"] + 20 * 1024}
    assert over_budget(usage(grown), budget) == ["dram_bytes: 160840 bytes, over the budget of 141000 by 19840"]


# A board without a budget is built ungated (the P4 until its first measured build); one the build no longer makes would
# be a budget nothing checks.
def test_every_budget_names_a_board_the_build_makes():
    workflow = (Path(__file__).resolve().parents[3] / ".github" / "workflows" / "embedded-build.yml").read_text()
    matrix = next(line for line in workflow.splitlines() if line.strip().startswith("env: ["))
    envs = [env.strip() for env in matrix.split("[", 1)[1].rstrip("]").split(",")]
    assert set(json.loads(BUDGET.read_text())["envs"]) <= set(envs)
