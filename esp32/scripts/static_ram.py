"""Fails when a board's static internal RAM outgrows its budget in esp32/static_ram_budget.json, ported from the Hexapod.

Internal RAM is what DMA, interrupts, task stacks and small allocations draw on, and what is left of it at runtime cannot
be measured in CI. What the linker places in it can: DRAM's .dram0.data and .dram0.bss, and IRAM's .iram0 sections, read
from the ELF. A change that needs more raises the board's budget in the same commit and says why there.

    python esp32/scripts/static_ram.py esp32-s3-n8r2 .pio/build/esp32-s3-n8r2/firmware.elf
"""
import json
import sys
from pathlib import Path

BUDGET = Path(__file__).resolve().parents[1] / "static_ram_budget.json"
DRAM_SECTIONS = (".dram0.data", ".dram0.bss")
IRAM_PREFIX = ".iram0."
KEYS = ("dram_bytes", "iram_bytes")


def usage(sections: dict[str, int]) -> dict[str, int]:
    """Static DRAM and IRAM in bytes, from section name -> size."""
    return {"dram_bytes": sum(sections.get(name, 0) for name in DRAM_SECTIONS),
            "iram_bytes": sum(size for name, size in sections.items() if name.startswith(IRAM_PREFIX))}


def over_budget(used: dict[str, int], budget: dict[str, int]) -> list[str]:
    return [f"{key}: {used[key]} bytes, over the budget of {budget[key]} by {used[key] - budget[key]}"
            for key in KEYS if used[key] > budget[key]]


def elf_sections(path: Path) -> dict[str, int]:
    from elftools.elf.elffile import ELFFile

    with path.open("rb") as f:
        return {section.name: section["sh_size"] for section in ELFFile(f).iter_sections()}


def main() -> int:
    env, elf = sys.argv[1], Path(sys.argv[2])
    used = usage(elf_sections(elf))
    budget = json.loads(BUDGET.read_text())["envs"].get(env)
    if budget is None:
        print(f"{env}: no budget in {BUDGET.name}, so not gated; "
              f"dram_bytes {used['dram_bytes']}, iram_bytes {used['iram_bytes']}")
        return 0
    for key in KEYS:
        print(f"{env} {key}: {used[key]} of {budget[key]} ({budget[key] - used[key]} left)")
    failures = over_budget(used, budget)
    for failure in failures:
        print(f"error: {env} {failure}; raise {BUDGET.name} only in a commit that justifies the growth", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
