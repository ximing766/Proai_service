import os
import pty
import time

def calc_checksum(data):
    return sum(data) & 0xFF

def start_sim():
    master, slave = pty.openpty()
    slave_name = os.ttyname(slave)
    
    symlink_path = '/tmp/ttyModule'
    try:
        os.unlink(symlink_path)
    except FileNotFoundError:
        pass
    os.symlink(slave_name, symlink_path)
    
    print(f"==================================================")
    print(f" MCU Simulator Started!")
    print(f" Virtual Serial Port for Main Service: {symlink_path}")
    print(f"==================================================")

    state = 0
    checksum = 0
    version = 0
    cmd = 0
    length = 0
    payload = bytearray()

    def send_frame(resp_cmd, resp_payload):
        resp_len = len(resp_payload)
        frame = bytearray([0x55, 0xAA, 0x00, resp_cmd, (resp_len >> 8) & 0xFF, resp_len & 0xFF])
        frame.extend(resp_payload)
        frame.append(calc_checksum(frame))
        os.write(master, frame)
        print(f"[MCU -> Module] CMD: 0x{resp_cmd:02X}, LEN: {resp_len}")

    def process_frame(cmd, payload):
        print(f"\n[Module -> MCU] CMD: 0x{cmd:02X}, LEN: {len(payload)}")
        if cmd == 0x00: # Heartbeat
            print("  -> Heartbeat received. Replying with 0x00 (First heartbeat)")
            send_frame(0x00, b'\x00')
        elif cmd == 0x01: # Product Info Query
            print("  -> Product Info Query received. Replying with PID and MCU version.")
            info = b'{"p":"rabbit_pid_001","v":"1.0.0"}'
            send_frame(0x01, info)
        elif cmd == 0x02: # Work mode
            print("  -> Work Mode Query received.")
            send_frame(0x02, b'\x00\x00')
        elif cmd == 0x03: # Net status
            print("  -> Net Status Report received.")
            send_frame(0x03, b'\x00')
        elif cmd == 0x08: # Status Query (DP Query)
            print("  -> Status Query received. Reporting all DPs.")
            # Format: DP_ID (1 byte), Type (1 byte), Length (2 bytes), Value (N bytes)
            dp_data = bytearray([0x01, 0x01, 0x00, 0x01, 0x01]) # dp 1, type bool, len 1, val True
            send_frame(0x07, dp_data)
        elif cmd == 0x06: # Command Issue (DP Cmd)
            print(f"  -> DP Command received. Data: {payload.hex()}")
            print("  -> Executing command... Done. Reporting new status.")
            send_frame(0x07, payload)
        elif cmd == 0x0A: # OTA Start
            print("  -> OTA Start received. Accepting...")
            send_frame(0x0A, b'\x00')
        elif cmd == 0x0B: # OTA Trans
            print("  -> OTA Data Packet received. Accepting...")
            send_frame(0x0B, b'\x00')
        else:
            print(f"  -> Unknown CMD: 0x{cmd:02X}")

    while True:
        try:
            data = os.read(master, 1)
        except OSError:
            time.sleep(0.01)
            continue
        
        if not data:
            continue
            
        b = data[0]
        
        if state == 0:
            if b == 0x55:
                state = 1
                checksum = b
        elif state == 1:
            if b == 0xAA:
                state = 2
                checksum = (checksum + b) & 0xFF
            else:
                state = 0
        elif state == 2:
            version = b
            checksum = (checksum + b) & 0xFF
            state = 3
        elif state == 3:
            cmd = b
            checksum = (checksum + b) & 0xFF
            state = 4
        elif state == 4:
            length = b << 8
            checksum = (checksum + b) & 0xFF
            state = 5
        elif state == 5:
            length |= b
            checksum = (checksum + b) & 0xFF
            payload = bytearray()
            if length > 0:
                state = 6
            else:
                state = 7
        elif state == 6:
            payload.append(b)
            checksum = (checksum + b) & 0xFF
            if len(payload) == length:
                state = 7
        elif state == 7:
            if checksum == b:
                process_frame(cmd, payload)
            else:
                print(f"Checksum Error! Expected {b:02X}, Calculated {checksum:02X}")
            state = 0

if __name__ == '__main__':
    start_sim()
