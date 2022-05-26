#!/usr/bin/env python3

"""
Converts lines of form
.word 0x00000000, 0x04000000, 0x00000000, 0xEE6666AA
to
    {{{     0,    -80,      0}, 0, {     0,      0}, {0xff, 0xff, 0xff, 0xff}}},
(example doesn't match)

typedef struct {
	short		ob[3];	/* x, y, z */
	unsigned short	flag;
	short		tc[2];	/* texture coord */
	unsigned char	cn[4];	/* color & alpha */
} Vtx_t;
"""

import sys, struct

Vtx_t = struct.Struct('>hhhHhhBBBB')

for line in sys.stdin:
	line = line.strip()
	assert line[:6] == '.word '
	w = ''.join(word[2:] for word in line[6:].split(', '))
	b = bytes.fromhex(w)
	x, y, z,  flag,  u, v,  r, g, b, a = Vtx_t.unpack(b)
	print(f'    {{{{{{{x:6}, {y:6}, {z:6}}},{flag:2}, {{{u:6}, {v:6}}}, {{0x{r:02x}, 0x{g:02x}, 0x{b:02x}, 0x{a:02x}}}}}}},')
