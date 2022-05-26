#!/usr/bin/env python3

"""
Converts asm lines with data (.byte, .halfword, .word, etc.) into hexadecimal comma-seperated lists
"""

import sys

directives = {
	'.byte': 1,
	'.halfword': 2,
	'.word': 4,
}

endian = 'big'
#endian = 'little'

for line in sys.stdin:
	line = line.strip()
	directive, _, value = line.partition(' ')
	bc = directives[directive]
	for v in value.split(','):
		i = int(v.strip(), 0)
		assert 0 <= i < (1 << (bc * 8)) # Check range
		if endian == 'little':
			for _ in range(bc):
				by = i & 0xFF
				i >>= 8
				sys.stdout.write(f'0x{by:X},')
		elif endian == 'big':
			for n in range(bc - 1, -1, -1):
				by = (i >> (n * 8)) & 0xFF
				sys.stdout.write(f'0x{by:X},')
		else:
			raise ValueError('Invalid endianness')
sys.stdout.write('\n')
