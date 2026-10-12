"""Compare firmware constants to the independently extracted PCB/symbol map."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/components/pcb2_board/include/pcb2_pins.h').read_text()
pins = {name: int(number) for name, number in re.findall(r'#define PCB2_PIN_(\w+) (\d+)', source)}
evidence = json.loads((root / 'hardware/evidence/hardware-map.json').read_text(encoding='utf-8'))
for name, pin in pins.items():
    net = 'RGB_DIN' if name == 'RGB' else name
    expected = evidence['gpio_map'][net]['gpio']
    assert pin == expected, f'{name}: firmware {pin}, PCB {expected}'
assert len(set(pins.values())) == len(pins), 'Peripheral pins collide'
pad_nets = evidence['pad_nets']
assert pad_nets['U10']['1'] == pad_nets['U9']['2'] == 'I2S_WS'
assert pad_nets['U10']['4'] == pad_nets['U9']['3'] == 'I2S_BCLK'
assert pad_nets['U10']['2'] == 'GND', 'Microphone must use left slot'
assert pad_nets['CARD1']['2'] == 'SD_CS'
for previous, following in [('LED3','LED4'),('LED4','LED5'),('LED5','LED6')]:
    assert pad_nets[previous]['1'] == pad_nets[following]['3']
print(f'PASS: {len(pins)} GPIO assignments, no collisions, I2S pad functions, left mic slot, SD CS and RGB chain')
