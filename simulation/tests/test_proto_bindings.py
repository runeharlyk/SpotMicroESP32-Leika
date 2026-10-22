"""The protocol bindings are gitignored and generated, so ones built before a schema gained a field are rebuilt rather
than silently dropping that field from every message the client sends and every reply it reads."""
import os

import pytest

from src.proto import BINDING, SCHEMAS, regenerate_if_stale


@pytest.fixture
def layout(tmp_path):
    schemas = tmp_path / "platform_shared"
    schemas.mkdir()
    for name in ("animation", "api", "filesystem", "message"):
        (schemas / f"{name}.proto").write_text("")
    ran = tmp_path / "ran"
    generator = tmp_path / "gen_protos.py"
    generator.write_text(f"open({str(ran)!r}, 'a').write('x')\n")
    binding = tmp_path / "message_pb2.py"
    binding.write_text("")
    return binding, schemas, generator, ran


def _age(path, seconds_ago):
    stamp = os.stat(path).st_mtime - seconds_ago
    os.utime(path, (stamp, stamp))


def _runs(ran):
    return len(ran.read_text()) if ran.exists() else 0


def test_bindings_older_than_any_schema_are_regenerated(layout):
    binding, schemas, generator, ran = layout
    _age(binding, 100)
    _age(schemas / "api.proto", 50)
    regenerate_if_stale(binding, schemas, generator)
    assert _runs(ran) == 1


def test_missing_bindings_are_generated(layout):
    binding, schemas, generator, ran = layout
    binding.unlink()
    regenerate_if_stale(binding, schemas, generator)
    assert _runs(ran) == 1


def test_bindings_newer_than_every_schema_are_kept(layout):
    binding, schemas, generator, ran = layout
    for schema in schemas.glob("*.proto"):
        _age(schema, 100)
    regenerate_if_stale(binding, schemas, generator)
    assert _runs(ran) == 0


def test_the_loaded_bindings_are_built_from_the_checked_in_schemas():
    from src.proto import message_pb2

    assert BINDING.stat().st_mtime >= max(schema.stat().st_mtime for schema in SCHEMAS.glob("*.proto"))
    assert message_pb2.AnalyticsData.DESCRIPTOR.fields_by_name["failed_allocations"].number == 20
