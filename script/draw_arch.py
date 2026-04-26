from graphviz import Digraph
import os

def draw_architecture():
    dot = Digraph(comment='ProAI Architecture', format='png')
    dot.attr(rankdir='TB')
    # Set font to something standard like Helvetica to avoid issues
    dot.attr('node', fontname='Helvetica')
    dot.attr('edge', fontname='Helvetica')
    dot.attr('graph', fontname='Helvetica')

    # Cloud Tier
    with dot.subgraph(name='cluster_cloud') as c:
        c.attr(label='Cloud Services', style='rounded', color='lightgrey')
        c.node('CloudLLM', 'Tongqu Voice Cloud\n(LLM & Audio)', shape='ellipse', style='filled', fillcolor='lightblue')
        c.node('TuyaCloud', 'Tuya IoT Cloud\n(Device Control)', shape='ellipse', style='filled', fillcolor='orange')

    # Device Tier (Linux)
    with dot.subgraph(name='cluster_device') as d:
        d.attr(label='ProAI Service (Linux)', style='filled', color='lightyellow')
        
        d.node('AgentSDK', 'Cloud LLM Module\n(Agent SDK)', shape='box', style='filled', fillcolor='white')
        d.node('TuyaSDK', 'Tuya IoT Module\n(Tuya Link SDK)', shape='box', style='filled', fillcolor='white')
        
        d.node('MainLoop', 'Main Event Loop\n(Dispatch & Sync)', shape='box', style='filled', fillcolor='lightgreen')
        
        d.node('ProtoParser', 'Tuya Protocol Parser\n(Frame Pack/Unpack)', shape='box', style='filled', fillcolor='white')
        d.node('OTAMgr', 'OTA Manager\n(Firmware Update)', shape='box', style='filled', fillcolor='white')
        d.node('UART', 'UART Interface', shape='box', style='filled', fillcolor='white')

        # Internal Device edges
        d.edge('AgentSDK', 'MainLoop', label='Voice/Text/IOT Cmds', dir='both')
        d.edge('TuyaSDK', 'MainLoop', label='DP Sync/Cmds', dir='both')
        
        d.edge('MainLoop', 'ProtoParser', label='Forward to MCU', dir='both')
        d.edge('MainLoop', 'OTAMgr', label='OTA Start/Progress', dir='both')
        
        d.edge('ProtoParser', 'UART', label='Packed Bytes', dir='both')
        d.edge('OTAMgr', 'UART', label='Firmware Chunks', dir='both')

    # MCU Tier
    with dot.subgraph(name='cluster_mcu') as m:
        m.attr(label='Downstream MCU', style='rounded')
        m.node('MCU', 'MCU\n(Hardware Control)', shape='box3d', style='filled', fillcolor='lightgrey')

    # Cloud <-> Device edges
    dot.edge('CloudLLM', 'AgentSDK', label='WSS (Voice/Text/IOT)', dir='both', penwidth='2.0')
    dot.edge('TuyaCloud', 'TuyaSDK', label='MQTT (DP Status/Control)', dir='both', penwidth='2.0')

    # Device <-> MCU edge
    dot.edge('UART', 'MCU', label='UART (Tuya MCU Protocol)\n55 AA Ver Cmd Len Data CS', dir='both', penwidth='2.0')

    # Render
    try:
        out_dir = os.path.dirname(os.path.abspath(__file__))
        output_path = dot.render('architecture', directory=out_dir, cleanup=True)
        print(f"Architecture diagram generated at: {os.path.abspath(output_path)}")
    except Exception as e:
        print(f"Error generating diagram: {e}")

if __name__ == '__main__':
    draw_architecture()