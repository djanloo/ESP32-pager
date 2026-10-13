import asyncio

from app.hub import Hub


class FakeConn:
    def __init__(self, fail: bool = False) -> None:
        self.sent: list = []
        self.closed_code: int | None = None
        self.fail = fail

    async def send_json(self, data) -> None:
        if self.fail:
            raise RuntimeError("connessione persa")
        self.sent.append(data)

    async def close(self, code: int = 1000) -> None:
        self.closed_code = code


class FakeClock:
    def __init__(self, t: float = 1000.0) -> None:
        self.t = t

    def __call__(self) -> float:
        return self.t


def make_hub(clock=None, cooldown_s: float = 10.0) -> Hub:
    return Hub(
        cooldown_s=cooldown_s,
        clock=clock or FakeClock(),
        wall_clock=lambda: 1760000000.7,
        demo_delay_s=0,
    )


def test_call_reaches_everyone_except_sender():
    hub = make_hub()
    a, b, c = FakeConn(), FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a)
        await hub.register("b", b)
        await hub.register("c", c)
        return await hub.call("a")

    assert asyncio.run(scenario()) == 2
    assert a.sent == []
    assert b.sent == [{"type": "call", "from": "a", "ts": 1760000000}]
    assert c.sent == b.sent


def test_call_from_unconnected_name_reaches_everyone():
    hub = make_hub()
    a, b = FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a)
        await hub.register("b", b)
        return await hub.call("pc")

    assert asyncio.run(scenario()) == 2
    assert a.sent[0]["from"] == "pc"


def test_cooldown_blocks_repeated_calls_from_same_sender():
    clock = FakeClock()
    hub = make_hub(clock=clock)
    b = FakeConn()

    async def scenario():
        await hub.register("b", b)
        first = await hub.call("a")
        clock.t += 9.9
        second = await hub.call("a")
        clock.t += 0.1
        third = await hub.call("a")
        return first, second, third

    assert asyncio.run(scenario()) == (1, None, 1)
    assert len(b.sent) == 2


def test_cooldown_is_per_sender():
    hub = make_hub()
    c = FakeConn()

    async def scenario():
        await hub.register("c", c)
        return await hub.call("a"), await hub.call("b")

    assert asyncio.run(scenario()) == (1, 1)


def test_duplicate_name_replaces_previous_connection():
    hub = make_hub()
    a1, a2, b = FakeConn(), FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a1)
        await hub.register("a", a2)
        await hub.register("b", b)
        await hub.call("b")

    asyncio.run(scenario())
    assert a1.closed_code == 1000
    assert a1.sent == []
    assert len(a2.sent) == 1
    assert hub.names() == ["a", "b"]


def test_unregister_of_replaced_connection_keeps_new_one():
    hub = make_hub()
    a1, a2 = FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a1)
        await hub.register("a", a2)

    asyncio.run(scenario())
    hub.unregister("a", a1)
    assert hub.names() == ["a"]
    hub.unregister("a", a2)
    assert hub.names() == []


def test_failed_send_removes_device_and_continues():
    hub = make_hub()
    broken, ok = FakeConn(fail=True), FakeConn()

    async def scenario():
        await hub.register("broken", broken)
        await hub.register("ok", ok)
        return await hub.call("pc")

    assert asyncio.run(scenario()) == 1
    assert len(ok.sent) == 1
    assert hub.names() == ["ok"]


def test_demo_answers_only_the_sender_as_someone_else():
    hub = make_hub()
    a, b = FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a)
        await hub.register("b", b)
        return await hub.demo("a")

    assert asyncio.run(scenario()) is True
    assert a.sent == [{"type": "call", "from": "demo", "ts": 1760000000, "demo": True}]
    assert b.sent == []


def test_demo_does_not_use_cooldown():
    hub = make_hub()
    a, b = FakeConn(), FakeConn()

    async def scenario():
        await hub.register("a", a)
        await hub.register("b", b)
        await hub.demo("a")
        await hub.demo("a")
        return await hub.call("a")

    assert asyncio.run(scenario()) == 1
    assert len(a.sent) == 2


def test_demo_from_unconnected_sender_is_dropped():
    hub = make_hub()
    b = FakeConn()

    async def scenario():
        await hub.register("b", b)
        return await hub.demo("a")

    assert asyncio.run(scenario()) is False
    assert b.sent == []


def test_demo_waits_before_answering():
    hub = Hub(demo_delay_s=0.05)
    a = FakeConn()

    async def scenario():
        await hub.register("a", a)
        task = asyncio.create_task(hub.demo("a"))
        await asyncio.sleep(0)
        sent_early = list(a.sent)
        await task
        return sent_early

    assert asyncio.run(scenario()) == []
    assert len(a.sent) == 1
