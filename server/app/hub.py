"""Registro dei cercapersone connessi e inoltro delle chiamate."""

import asyncio
import logging
import time
from typing import Any, Callable, Protocol

log = logging.getLogger("pager.hub")

DEMO_FROM = "demo"  # mittente finto delle risposte alle chiamate dimostrative


class Connection(Protocol):
    async def send_json(self, data: Any) -> None: ...

    async def close(self, code: int = 1000) -> None: ...


class Hub:
    def __init__(
        self,
        cooldown_s: float = 10.0,
        clock: Callable[[], float] = time.monotonic,
        wall_clock: Callable[[], float] = time.time,
        demo_delay_s: float = 1.0,
    ) -> None:
        self._cooldown_s = cooldown_s
        self._demo_delay_s = demo_delay_s
        self._clock = clock
        self._wall_clock = wall_clock
        self._conns: dict[str, Connection] = {}
        self._last_call: dict[str, float] = {}

    def names(self) -> list[str]:
        return sorted(self._conns)

    async def register(self, name: str, conn: Connection) -> None:
        old = self._conns.get(name)
        self._conns[name] = conn
        log.info("connesso: %s", name)
        if old is not None and old is not conn:
            log.info("sostituita la connessione precedente di %s", name)
            try:
                await old.close(code=1000)
            except Exception:
                pass

    def unregister(self, name: str, conn: Connection) -> None:
        if self._conns.get(name) is conn:
            del self._conns[name]
            log.info("disconnesso: %s", name)

    async def call(self, from_name: str) -> int | None:
        now = self._clock()
        last = self._last_call.get(from_name)
        if last is not None and now - last < self._cooldown_s:
            log.info("chiamata di %s ignorata (anti-spam)", from_name)
            return None
        self._last_call[from_name] = now

        message = {"type": "call", "from": from_name, "ts": int(self._wall_clock())}
        delivered = 0
        for name, conn in list(self._conns.items()):
            if name == from_name:
                continue
            try:
                await conn.send_json(message)
                delivered += 1
            except Exception as exc:
                log.warning("invio a %s fallito (%s): rimosso", name, exc)
                self.unregister(name, conn)
        log.info("chiamata di %s consegnata a %d dispositivi", from_name, delivered)
        return delivered

    async def demo(self, from_name: str) -> bool:
        """Chiamata dimostrativa: dopo un breve ritardo (per lasciar vedere il lampo
        verde) risponde solo al mittente, come se l'avesse chiamato DEMO_FROM.
        Non tocca l'anti-spam. Ritorna False se il mittente non è più connesso."""
        log.info("chiamata dimostrativa da %s", from_name)
        await asyncio.sleep(self._demo_delay_s)
        conn = self._conns.get(from_name)
        if conn is None:
            log.info("risposta dimostrativa non consegnata: %s non è più connesso", from_name)
            return False
        message = {"type": "call", "from": DEMO_FROM, "ts": int(self._wall_clock()), "demo": True}
        try:
            await conn.send_json(message)
        except Exception as exc:
            log.warning("invio a %s fallito (%s): rimosso", from_name, exc)
            self.unregister(from_name, conn)
            return False
        log.info("risposta dimostrativa inviata a %s", from_name)
        return True
