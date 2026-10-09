"""Cercapersone finto per test manuali: si connette, stampa le chiamate ricevute,
con --call ne invia una appena connesso."""

import argparse
import asyncio
import json

import websockets


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="ws://127.0.0.1:8000/ws")
    parser.add_argument("--name", default="finto")
    parser.add_argument("--call", action="store_true", help="invia una chiamata appena connesso")
    parser.add_argument("--seconds", type=float, default=30, help="per quanto restare in ascolto")
    args = parser.parse_args()

    async with websockets.connect(f"{args.url}?name={args.name}") as ws:
        print(f"connesso come {args.name}", flush=True)
        if args.call:
            await ws.send(json.dumps({"type": "call"}))
            print("chiamata inviata", flush=True)
        try:
            async with asyncio.timeout(args.seconds):
                async for raw in ws:
                    print(f"ricevuto: {raw}", flush=True)
        except TimeoutError:
            pass


asyncio.run(main())
