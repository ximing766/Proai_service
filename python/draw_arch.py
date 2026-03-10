from graphviz import Digraph
import os

def draw_architecture():
    dot = Digraph(comment='ProAI Architecture', format='png')
    dot.attr(rankdir='TB')
    # Set font to something standard like Helvetica to avoid issues, though default is usually fine for English
    dot.attr('node', fontname='Helvetica')
    dot.attr('edge', fontname='Helvetica')
    dot.attr('graph', fontname='Helvetica')

    # AI Module Cluster
    with dot.subgraph(name='cluster_ai_module') as c:
        c.attr(label='AI Module', style='rounded')
        
        # Master Service (Simplified)
        c.node('MasterService', 'Master Service', shape='box3d', style='filled', fillcolor='lightgrey')
        
        # Slave Service
        with c.subgraph(name='cluster_slave') as s:
            s.attr(label='Slave Service', style='filled', color='lightblue')
            s.node('SerialMgr', 'Serial Manager')
            s.node('ProtoParser', 'Protocol Parser\n(Tuya)')
            s.node('IPC_Client', 'IPC Client\n(JSON Handler)')
            s.node('OTAMgr', 'OTA Dispatcher')
            
            # Internal Slave edges
            s.edge('SerialMgr', 'ProtoParser', label='Bytes')
            s.edge('ProtoParser', 'IPC_Client', label='Parsed Data')
            s.edge('SerialMgr', 'OTAMgr')

        # IPC Connection
        # Using a record node or label to show the interface details
        ipc_label = 'JSON over TCP (Port 5555)\n\n' \
                    'Interfaces:\n' \
                    '1. send_mcu(cmd, payload)\n' \
                    '2. evt_mcu(cmd, payload)\n' \
                    '3. send_slave(code, payload)\n' \
                    '4. evt_slave(code, payload)'
        
        dot.edge('MasterService', 'IPC_Client', label=ipc_label, dir='both', penwidth='2.0')

    # MCU Cluster
    with dot.subgraph(name='cluster_mcu') as c:
        c.attr(label='Seat MCU', style='rounded')
        c.node('SeatCtrl', 'Seat Control')
        c.node('Sensors', 'Sensors')
        c.edge('SeatCtrl', 'Sensors')

    # Slave <-> MCU
    # Detailed interaction label
    uart_label = 'UART (Tuya Protocol)\n' \
                 '55 AA Ver Cmd Len Data CS'
    dot.edge('SerialMgr', 'SeatCtrl', label=uart_label, dir='both', penwidth='2.0')

    # Render
    try:
        output_path = dot.render('architecture', directory='.', cleanup=True)
        print(f"Architecture diagram generated at: {output_path}")
    except Exception as e:
        print(f"Error generating diagram: {e}")

if __name__ == '__main__':
    draw_architecture()
