import subprocess

lookup_table = {
    'push': 2,
    'mov': 1,
    'in': 1,
    'eor': 1,
    'lds': 2,
    'movw': 1,
    'subi': 1,
    'sbci': 1,
    'sts': 2,
    'asr': 1,
    'add': 1,
    'adc': 1,
    'sbrc': 2,
    'adiw': 2,
    'ror': 1,
    'st': 2,
    'pop': 2,
    'out': 1,
    'reti': 4
}

proc = subprocess.Popen("avr-objdump -M intel -D build/arduino.avr.uno/Progetto.ino.elf | awk -v RS= '/^[[:xdigit:]]+ <__vector_21>/'", shell=True, stdout=subprocess.PIPE)
func = proc.stdout.read().decode()

print(func)

disasm = func.split('\n')[1:-1]
opcodes = [x.split('\t')[2] for x in disasm]

sum = 0

for o in opcodes:
    sum += lookup_table[o]

print(f'Cycles: {sum}')
