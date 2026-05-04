#!/usr/bin/env python3
import argparse
import json
import socket
import sys


def send_json(client: socket.socket, payload: dict):
    data = json.dumps(payload, ensure_ascii=False) + "\n"
    client.sendall(data.encode("utf-8"))
    resp = client.recv(1024).decode("utf-8", errors="ignore").strip()
    return resp


def main():
    parser = argparse.ArgumentParser(description="Local voice IPC simulator")
    parser.add_argument("--host", default="127.0.0.1", help="Main service host")
    parser.add_argument("--port", type=int, default=19090, help="Main service command TCP port")
    parser.add_argument("--intent", choices=["heater_on", "heater_off"], help="Send semantic intent")
    parser.add_argument("--ping", action="store_true", help="Send ping message")
    parser.add_argument("--interactive", action="store_true", help="Keep TCP connection and send multiple commands")
    args = parser.parse_args()

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client:
            client.connect((args.host, args.port))

            if args.interactive:
                print("Connected. input: ping | heater_on | heater_off | quit")
                while True:
                    cmd = input("> ").strip()
                    if cmd in ("quit", "exit"):
                        break
                    if cmd == "ping":
                        payload = {"type": "ping"}
                    elif cmd in ("heater_on", "heater_off"):
                        payload = {"type": "intent", "intent": cmd, "source": "local_voice"}
                    else:
                        print("Unsupported command")
                        continue

                    resp = send_json(client, payload)
                    print("request:", json.dumps(payload, ensure_ascii=False))
                    print("response:", resp)
            else:
                if args.ping:
                    payload = {"type": "ping"}
                elif args.intent:
                    payload = {"type": "intent", "intent": args.intent, "source": "local_voice"}
                else:
                    print("Need one of: --ping | --intent | --interactive", file=sys.stderr)
                    return 2

                resp = send_json(client, payload)
                print("request:", json.dumps(payload, ensure_ascii=False))
                print("response:", resp)
    except Exception as exc:
        print(f"TCP request failed: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
