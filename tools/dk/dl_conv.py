#!/usr/bin/env python3

"""
Converts lines of form
.word 0xBF000000, 0x0028148C
to
    gsSP1Triangle(4, 2, 14, 0),
"""

import sys

for line in sys.stdin:
	line = line.strip()
	assert line[:6] == '.word '
	x, y = line[6:].split(', ')
	x = int(x, 0)
	y = int(y, 0)
	if x >> 24 == 0xBF:
		c = (y & 0xFF) // 10
		assert y & 0xFF == c * 10
		y >>= 8
		b = (y & 0xFF) // 10
		assert y & 0xFF == b * 10
		y >>= 8
		a = (y & 0xFF) // 10
		assert y & 0xFF == a * 10
		y >>= 8
		print(f'    gsSP1Triangle({a}, {b}, {c}, 0),')
