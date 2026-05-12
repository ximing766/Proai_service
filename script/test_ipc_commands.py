import socket
import json
import time

def send_ipc_command(cmd_dict):
    """发送单个指令到本地 IPC 服务器"""
    host = '127.0.0.1'
    port = 19090
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(2.0)
            s.connect((host, port))
            # 确保每条指令以换行符结尾
            msg = json.dumps(cmd_dict) + "\n"
            s.sendall(msg.encode('utf-8'))
            response = s.recv(1024)
            print(f"Sent: {msg.strip()}")
            print(f"Recv: {response.decode('utf-8').strip()}")
    except Exception as e:
        print(f"Error sending {cmd_dict}: {e}")

def main():
    # 从文档中提取的所有指令列表
    commands = [
        # 4.1 通风控制
        {"type":"intent","intent":"set_fanSw","value":True,"source":"local_voice"},
        {"type":"intent","intent":"set_fanSw","value":False,"source":"local_voice"},
        
        # 4.2 加热控制
        {"type":"intent","intent":"set_hotSw","value":True,"source":"local_voice"},
        {"type":"intent","intent":"set_hotSw","value":False,"source":"local_voice"},
        
        # 4.3 自动模式
        {"type":"intent","intent":"set_autoMode","value":True,"source":"local_voice"},
        {"type":"intent","intent":"set_autoMode","value":False,"source":"local_voice"},
        
        # 4.4 氛围灯控制
        {"type":"intent","intent":"set_F_light","value":True,"source":"local_voice"},
        {"type":"intent","intent":"set_F_light","value":False,"source":"local_voice"},
        
        # 4.5 音量与静音控制
        {"type":"intent","intent":"set_mute_mode_switch","value":True,"source":"local_voice"},
        {"type":"intent","intent":"set_mute_mode_switch","value":False,"source":"local_voice"},
        
        # 4.6 侧保护开关
        {"type":"intent","intent":"set_protectionLeftSw","value":True,"source":"local_voice"},
        {"type":"intent","intent":"set_protectionLeftSw","value":False,"source":"local_voice"},
        {"type":"intent","intent":"set_protectionRightSw","value":True,"source":"local_voice"},
        {"type":"intent","intent":"set_protectionRightSw","value":False,"source":"local_voice"},
        
        # 4.7 网络与设备绑定
        {"type":"intent","intent":"set_cloudUnbund","value":True,"source":"local_voice"},
        
        # 4.8 状态查询
        {"type":"intent","intent":"get_rssi","source":"local_voice"},
        {"type":"intent","intent":"get_batPercent","source":"local_voice"},
        
        # 4.9 旋转控制
        {"type":"intent","intent":"set_auto_rotate","value":True,"source":"local_voice"},
        {"type":"intent","intent":"set_auto_rotate","value":False,"source":"local_voice"},
        {"type":"intent","intent":"set_assist_rotate","value":True,"source":"local_voice"},
        {"type":"intent","intent":"set_assist_rotate","value":False,"source":"local_voice"},
        {"type":"intent","intent":"set_rotate_command","value":1,"source":"local_voice"},
        {"type":"intent","intent":"set_rotate_command","value":2,"source":"local_voice"},
        {"type":"intent","intent":"set_rotate_command","value":3,"source":"local_voice"},
        {"type":"intent","intent":"set_rotate_command","value":4,"source":"local_voice"},
        
        # 4.10 座椅安装位置
        {"type":"intent","intent":"set_installation_position","value":False,"source":"local_voice"},
        {"type":"intent","intent":"set_installation_position","value":True,"source":"local_voice"}
    ]

    print(f"Starting to send {len(commands)} commands with 5s interval...")
    
    while True:
        for cmd in commands:
            send_ipc_command(cmd)
            time.sleep(5)
        print("Completed one full cycle. Restarting...")

if __name__ == "__main__":
    main()
