#!/usr/bin/env python3
"""Type text into the VM via QEMU monitor scancodes, mapped for Windows' Spanish (es-ES) layout.
   ./sendkeys.py 'text'   |  ./sendkeys.py --key ctrl-shift-ret"""
import socket, sys, time, os
os.chdir(os.path.dirname(os.path.realpath(__file__)))
ES = {' ': 'spc', '%': 'shift-5', ':': 'shift-dot', '\\': 'ctrl-alt-less', '(': 'shift-8', ')': 'shift-9',
      '-': 'slash', '.': 'dot', '_': 'shift-slash', '@': 'ctrl-alt-2', '/': 'shift-7', '"': 'shift-2',
      '=': 'shift-0', ',': 'comma', ';': 'shift-comma', '\n': 'ret', "'": 'minus', '&': 'shift-6',
      '$': 'shift-4', '|': 'ctrl-alt-1', '+': 'bracket_right', '*': 'shift-bracket_right', '!': 'shift-1'}
def key(name):
    s = socket.socket(socket.AF_UNIX); s.connect('monitor.sock'); s.recv(4096)
    s.sendall(f'sendkey {name}\n'.encode()); time.sleep(0.04); s.close()
if sys.argv[1] == '--key':
    for k in sys.argv[2:]: key(k); time.sleep(0.3)
else:
    for ch in sys.argv[1]:
        if ch.isalpha() and ch.isascii(): key(('shift-' if ch.isupper() else '') + ch.lower())
        elif ch.isdigit(): key(ch)
        else: key(ES[ch])
