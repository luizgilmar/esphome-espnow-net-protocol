import ast
from pathlib import Path
from types import SimpleNamespace

def validator():
    p = Path(__file__).resolve().parents[1]/"components/espnow_net_protocol/__init__.py"
    tree = ast.parse(p.read_text(encoding="utf-8"))
    fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == "_validate_heartbeat")
    namespace = {"cv": SimpleNamespace(Invalid=ValueError)}
    exec(compile(ast.Module(body=[fn], type_ignores=[]), str(p), "exec"), namespace)
    return namespace[fn.name]

def config(peer="hub", interval=5000, timeout=2000):
    return {"peers": [{"id": "hub"}], "heartbeat": {"peer":peer,
            "send_interval":SimpleNamespace(total_milliseconds=interval),
            "timeout":SimpleNamespace(total_milliseconds=timeout)}}

def test_heartbeat_disabled_by_default():
    assert validator()({}) == {}

def test_heartbeat_probe_and_responder():
    for interval in (0, 5000):
        c = config(interval=interval)
        assert validator()(c) is c

def test_heartbeat_invalid_peer_or_timing():
    for c in (config(peer="unknown"), config(timeout=0), config(timeout=60001),
              config(interval=2000), config(interval=1000), config(interval=3600001)):
        try:
            validator()(c)
        except ValueError:
            continue
        raise AssertionError("Invalid heartbeat configuration accepted")
