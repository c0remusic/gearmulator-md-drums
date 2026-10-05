"""Editor knob atlas: a dark cap, a track arc and the value in an orange arc.

128 frames (one per MIDI value), 96 px each, 16 per row. Drawn at 4x and reduced for clean edges.
Usage: python gen_knob_atlas.py <out.png> [preview.png] [spritesheet.rcss]
The editor's copy is source/jucePluginData/elektronEditorKnobAtlas.png; the spritesheet block it prints
goes in elektron_editor.rcss (@spritesheet mdEdKnobSheet).
"""
import math
import sys

from PIL import Image, ImageDraw

FRAME = 96
SCALE = 4
FRAMES = 128
COLUMNS = 16

TRACK = (54, 56, 62, 255)
VALUE = (255, 107, 31, 255)
POINTER = (244, 241, 234, 255)
CAP_TOP = (58, 60, 67)
CAP_BOTTOM = (30, 31, 35)
RIM_LIGHT = (78, 81, 89, 255)
RIM_DARK = (18, 19, 22, 255)

START = 135.0		# degrees, PIL convention (0 = 3 o'clock, clockwise): 7:30
SWEEP = 270.0		# to 4:30


def point(_cx, _cy, _radius, _degrees):
	a = math.radians(_degrees)
	return _cx + _radius * math.cos(a), _cy + _radius * math.sin(a)


def dot(_draw, _x, _y, _radius, _colour):
	_draw.ellipse((_x - _radius, _y - _radius, _x + _radius, _y + _radius), fill=_colour)


def frame(_value):
	size = FRAME * SCALE
	image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
	draw = ImageDraw.Draw(image)
	c = size / 2
	arcRadius = 44 * SCALE
	arcWidth = 7 * SCALE
	box = (c - arcRadius, c - arcRadius, c + arcRadius, c + arcRadius)

	# The track, round ended
	draw.arc(box, START, START + SWEEP, fill=TRACK, width=arcWidth)
	for angle in (START, START + SWEEP):
		x, y = point(c, c, arcRadius - arcWidth / 2, angle)
		dot(draw, x, y, arcWidth / 2, TRACK)

	# The value
	end = START + SWEEP * _value / (FRAMES - 1)
	if _value > 0:
		draw.arc(box, START, end, fill=VALUE, width=arcWidth)
		for angle in (START, end):
			x, y = point(c, c, arcRadius - arcWidth / 2, angle)
			dot(draw, x, y, arcWidth / 2, VALUE)

	# The cap: a vertical gradient in a rim lit from above
	capRadius = 31 * SCALE
	cap = Image.new("RGBA", (size, size), (0, 0, 0, 0))
	gradient = Image.new("RGBA", (size, size), (0, 0, 0, 0))
	gdraw = ImageDraw.Draw(gradient)
	top, bottom = c - capRadius, c + capRadius
	for y in range(int(top), int(bottom) + 1):
		t = (y - top) / (bottom - top)
		colour = tuple(int(CAP_TOP[i] + (CAP_BOTTOM[i] - CAP_TOP[i]) * t) for i in range(3)) + (255,)
		gdraw.line((0, y, size, y), fill=colour)
	mask = Image.new("L", (size, size), 0)
	ImageDraw.Draw(mask).ellipse((c - capRadius, c - capRadius, c + capRadius, c + capRadius), fill=255)
	cap.paste(gradient, (0, 0), mask)
	rim = ImageDraw.Draw(cap)
	rim.arc((c - capRadius, c - capRadius, c + capRadius, c + capRadius), 180, 360, fill=RIM_LIGHT, width=int(1.5 * SCALE))
	rim.arc((c - capRadius, c - capRadius, c + capRadius, c + capRadius), 0, 180, fill=RIM_DARK, width=int(1.5 * SCALE))
	image = Image.alpha_composite(image, cap)

	# The pointer
	draw = ImageDraw.Draw(image)
	inner = point(c, c, 13 * SCALE, end)
	outer = point(c, c, 25 * SCALE, end)
	draw.line((inner, outer), fill=POINTER, width=4 * SCALE)
	for x, y in (inner, outer):
		dot(draw, x, y, 2 * SCALE, POINTER)

	return image.resize((FRAME, FRAME), Image.LANCZOS)


def spritesheet(_filename):
	lines = ["@spritesheet mdEdKnobSheet", "{"]
	for value in range(FRAMES):
		lines.append("\tmdEdKnob_%03d: %dpx %dpx %dpx %dpx;" % (value, (value % COLUMNS) * FRAME, (value // COLUMNS) * FRAME, FRAME, FRAME))
	lines.append("\tsrc: %s;" % _filename)
	lines.append("}")
	return "\n".join(lines) + "\n"


def main(_out, _preview=None, _sheet=None):
	rows = (FRAMES + COLUMNS - 1) // COLUMNS
	atlas = Image.new("RGBA", (COLUMNS * FRAME, rows * FRAME), (0, 0, 0, 0))
	for value in range(FRAMES):
		atlas.paste(frame(value), ((value % COLUMNS) * FRAME, (value // COLUMNS) * FRAME))
	atlas.save(_out, optimize=True)
	if _sheet:
		import os
		with open(_sheet, "w", encoding="utf-8", newline="\n") as f:
			f.write(spritesheet(os.path.basename(_out)))
	if _preview:
		# A few values on the editor's panel colour, at the size the editor shows them and twice it
		values = [0, 16, 32, 64, 96, 127]
		preview = Image.new("RGBA", (len(values) * 110, 160), (28, 29, 31, 255))
		for i, value in enumerate(values):
			f = frame(value)
			preview.alpha_composite(f.resize((46, 46), Image.LANCZOS), (i * 110 + 32, 8))
			preview.alpha_composite(f, (i * 110 + 7, 60))
		preview.save(_preview)


if __name__ == "__main__":
	main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else None, sys.argv[3] if len(sys.argv) > 3 else None)
