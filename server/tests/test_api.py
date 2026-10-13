import time

import pytest
from fastapi.testclient import TestClient
from starlette.websockets import WebSocketDisconnect

from app.hub import Hub
from app.main import create_app


@pytest.fixture
def client():
    with TestClient(create_app(Hub(cooldown_s=10, demo_delay_s=0))) as c:
        yield c


def wait_devices(client: TestClient, expected: list[str]) -> None:
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        if client.get("/devices").json()["devices"] == expected:
            return
        time.sleep(0.01)
    raise AssertionError(f"dispositivi attesi {expected}, trovati {client.get('/devices').json()}")


def test_health(client):
    assert client.get("/health").json() == {"status": "ok"}


def test_devices_lists_connected_and_forgets_disconnected(client):
    assert client.get("/devices").json() == {"devices": []}
    with client.websocket_connect("/ws?name=b"), client.websocket_connect("/ws?name=a"):
        wait_devices(client, ["a", "b"])
    wait_devices(client, [])


def test_ws_call_reaches_others_but_not_sender(client):
    with client.websocket_connect("/ws?name=a") as a, client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        a.send_json({"type": "call"})
        msg = b.receive_json()
        assert msg["type"] == "call" and msg["from"] == "a" and isinstance(msg["ts"], int)
        # Se "a" avesse ricevuto la propria chiamata, il primo messaggio sarebbe quello.
        b.send_json({"type": "call"})
        assert a.receive_json()["from"] == "b"


def test_ws_demo_answers_only_the_sender(client):
    with client.websocket_connect("/ws?name=a") as a, client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        a.send_json({"type": "demo"})
        msg = a.receive_json()
        assert msg["type"] == "call" and msg["from"] == "demo" and msg["demo"] is True
        assert isinstance(msg["ts"], int)
        # Se la demo fosse arrivata anche a b, sarebbe lei il suo primo messaggio.
        a.send_json({"type": "call"})
        assert b.receive_json()["from"] == "a"


def test_http_call_delivers_to_connected_devices(client):
    with client.websocket_connect("/ws?name=a") as a, client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        resp = client.post("/call", json={"from": "pc"})
        assert resp.status_code == 200
        assert resp.json() == {"delivered": 2}
        assert a.receive_json()["from"] == "pc"
        assert b.receive_json()["from"] == "pc"


def test_http_call_excludes_device_with_same_name(client):
    with client.websocket_connect("/ws?name=a"), client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        assert client.post("/call", json={"from": "a"}).json() == {"delivered": 1}
        assert b.receive_json()["from"] == "a"


def test_cooldown_is_shared_between_ws_and_http(client):
    with client.websocket_connect("/ws?name=a") as a, client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        a.send_json({"type": "call"})
        assert b.receive_json()["from"] == "a"
        assert client.post("/call", json={"from": "a"}).status_code == 429


@pytest.mark.parametrize("query", ["", "?name=", "?name=Giulia", "?name=a%20b", "?name=" + "x" * 33])
def test_invalid_name_is_rejected(client, query):
    with client.websocket_connect("/ws" + query) as ws:
        with pytest.raises(WebSocketDisconnect) as exc:
            ws.receive_json()
    assert exc.value.code == 1008
    assert client.get("/devices").json() == {"devices": []}


@pytest.mark.parametrize("body", [{}, {"from": ""}, {"from": "Giulia"}, {"from": "x" * 33}, {"from": 3}])
def test_http_call_with_invalid_from_is_422(client, body):
    assert client.post("/call", json=body).status_code == 422


def test_duplicate_name_closes_previous_connection(client):
    with client.websocket_connect("/ws?name=a") as a1:
        wait_devices(client, ["a"])
        with client.websocket_connect("/ws?name=a") as a2:
            with pytest.raises(WebSocketDisconnect) as exc:
                a1.receive_json()
            assert exc.value.code == 1000
            wait_devices(client, ["a"])
            assert client.post("/call", json={"from": "pc"}).json() == {"delivered": 1}
            assert a2.receive_json()["from"] == "pc"


def test_malformed_messages_are_ignored_and_connection_stays_open(client):
    with client.websocket_connect("/ws?name=a") as a, client.websocket_connect("/ws?name=b") as b:
        wait_devices(client, ["a", "b"])
        a.send_text("non è json")
        a.send_text('{"type": "boh"}')
        a.send_text("[1, 2]")
        a.send_bytes(b"\x00\x01")
        a.send_json({"type": "call", "extra": 1})
        assert b.receive_json()["from"] == "a"
