/**
	This file is part of FORTMAX.

	FORTMAX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	FORTMAX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with FORTMAX.  If not, see <http://www.gnu.org/licenses/>.

	Copyright: Martin K. Schröder (info@fortmax.se) 2014
*/

#include <ctype.h>
//#include <math.h>
#include <stdlib.h>

#include "vt100.h"

#define KEY_ESC 0x1b
#define KEY_DEL 0x7f
#define KEY_BELL 0x07

#define STATE(NAME, TERM, EV, ARG) void NAME(struct vt100* TERM, uint8_t EV, uint16_t ARG)

// states
enum { STATE_IDLE, STATE_ESCAPE, STATE_COMMAND };

// events that are passed into states
enum {
	EV_CHAR = 1,
};

#define MAX_COMMAND_ARGS 4
static struct vt100 {
	union flags {
		uint8_t val;
		struct {
			// 0 = cursor remains on last column when it gets there
			// 1 = lines wrap after last column to next line
			uint8_t cursor_wrap : 1;
			uint8_t scroll_mode : 1;
			uint8_t origin_mode : 1;
		};
	} flags;

	//uint16_t screen_width, screen_height;
	// cursor position on the screen (0, 0) = top left corner.
	int16_t cursor_x, cursor_y;
	int16_t saved_cursor_x, saved_cursor_y; // used for cursor save restore
	int16_t scroll_start_row, scroll_end_row;
	// character width and height
	int8_t char_width, char_height;
	// Screem size in pixels
	uint16_t screen_width, screen_height;
	// Screen size in characters
	uint16_t row_count, col_count;
	// colors used for rendering current characters
	uint16_t back_color, front_color;
	// the starting y-position of the screen scroll
	uint16_t scroll_value;
	// command arguments that get parsed as they appear in the terminal
	uint8_t narg;
	uint16_t args[MAX_COMMAND_ARGS];
	// current arg pointer (we use it for parsing)
	uint8_t carg;

	void (*state)(struct vt100* term, uint8_t ev, uint16_t arg);
	void (*ret_state)(struct vt100* term, uint8_t ev, uint16_t arg);

	DisplayDevice* display;
	VT100Callbacks* callbacks;
} term;

STATE(_st_idle, term, ev, arg);
STATE(_st_esc_sq_bracket, term, ev, arg);
STATE(_st_esc_question, term, ev, arg);
STATE(_st_esc_hash, term, ev, arg);

void _vt100_reset()
{
	//term.screen_width = term.screen_width;
	//term.screen_height = term.screen_height;
	term.char_height = term.display->getCharHeight();
	term.char_width = term.display->getCharWidth();
	term.screen_width = term.display->getWidth();
	term.screen_height = term.display->getHeight();
	term.row_count = term.screen_height / term.char_height;
	term.col_count = term.screen_width / term.char_width;
	term.back_color = 0x0000;
	term.front_color = 0xffff;
	term.cursor_x = term.cursor_y = term.saved_cursor_x = term.saved_cursor_y = 0;
	term.narg = 0;
	term.state = _st_idle;
	term.ret_state = 0;
	term.scroll_value = 0;
	term.scroll_start_row = 0;
	term.scroll_end_row = term.row_count; // outside of screen = whole screen scrollable
	term.flags.cursor_wrap = 0;
	term.flags.origin_mode = 0;
	term.display->setFrontColor(term.front_color);
	term.display->setBackColor(term.back_color);
	term.display->setScrollMargins(0, 0);
	term.display->setScrollStart(0);
}

void _vt100_resetScroll()
{
	term.scroll_start_row = 0;
	term.scroll_end_row = term.row_count;
	term.scroll_value = 0;
	term.display->setScrollMargins(0, 0);
	term.display->setScrollStart(0);
}

#define VT100_CURSOR_X(TERM) (TERM->cursor_x * TERM->char_width)

inline uint16_t VT100_CURSOR_Y(struct vt100* t)
{
	// if within the top or bottom margin areas then normal addressing
	if(t->cursor_y < t->scroll_start_row || t->cursor_y >= t->scroll_end_row) {
		return t->cursor_y * t->char_height;
	} else {
		// otherwise we are inside scroll area
		uint16_t scroll_height = t->scroll_end_row - t->scroll_start_row;
		uint16_t row = t->cursor_y + t->scroll_value;
		if(t->cursor_y + t->scroll_value >= t->scroll_end_row)
			row -= scroll_height;
		// if scroll_value == 0: y = t->cursor_y;
		// if scroll_value == 1 && scroll_start_row == 2 && scroll_end_row == 38:
		// 		y = t->cursor_y + scroll_value;
		//uint16_t row = (t->cursor_y - t->scroll_start_row) % scroll_height;
		/*uint16_t skip = t->scroll_value - t->scroll_start_row; 
		uint16_t row = t->cursor_y + skip;
		uint16_t scroll_height = t->scroll_end_row - t->scroll_start_row; 
		//row = (row % scroll_height);// + t->scroll_start_row;*/
		return row * t->char_height;
	}
	/*uint16_t y = 0;
	if(t->cursor_y >= t->top_margin && t->cursor_y < t->bottom_margin){
		y = t->cursor_y * term->char_height;
		if(t->scroll >= (t->top_margin * term->char_height)){
			y += t->scroll - t->top_margin * term->char_height;
		}
	} else if(t->cursor_y < t->top_margin){
		y = (t->cursor_y * term->char_height);
	} else if(t->cursor_y >= t->bottom_margin){
		y = (t->cursor_y * term->char_height);
		if(t->scroll >= (t->top_margin * term->char_height)){
			y += t->scroll - t->top_margin * term->char_height;
		}
	}
	//y = ((t->cursor_y - (term.row_count - t->bottom_margin)) * term->char_height);// % term.screen_height;
	//y = ((t->cursor_y * term->char_height) + t->scroll) % term.screen_height;
	return y % term.screen_height;*/
}

void _vt100_clearLines(struct vt100* t, uint16_t start_line, uint16_t end_line)
{
	for(int c = start_line; c <= end_line; c++) {
		uint16_t cy = t->cursor_y;
		t->cursor_y = c;
		t->display->fillRect(0, VT100_CURSOR_Y(t), t->screen_width, t->char_height, 0x0000);
		t->cursor_y = cy;
	}
	/*uint16_t start = ((start_line * t->char_height) + t->scroll) % term.screen_height;
	uint16_t h = (end_line - start_line) * term->char_height;
	term.display->fillRect(0, start, term.screen_width, h, 0x0000); */
}

// scrolls the scroll region up (lines > 0) or down (lines < 0)
void _vt100_scroll(struct vt100* t, int16_t lines)
{
	if(!lines)
		return;

	// get height of scroll area in rows
	uint16_t scroll_height = t->scroll_end_row - t->scroll_start_row;
	// clearing of lines that we have scrolled up or down
	if(lines > 0) {
		_vt100_clearLines(t, t->scroll_start_row, t->scroll_start_row + lines - 1);
		// update the scroll value (wraps around scroll_height)
		t->scroll_value = (t->scroll_value + lines) % scroll_height;
		// scrolling up so clear first line of scroll area
		//uint16_t y = (t->scroll_start_row + t->scroll_value) * term->char_height;
		//term.display->fillRect(0, y, term.screen_width, lines * term->char_height, 0x0000);
	} else if(lines < 0) {
		_vt100_clearLines(t, t->scroll_end_row - lines, t->scroll_end_row - 1);
		// make sure that the value wraps down
		t->scroll_value = (scroll_height + t->scroll_value + lines) % scroll_height;
		// scrolling down - so clear last line of the scroll area
		//uint16_t y = (t->scroll_start_row + t->scroll_value) * term->char_height;
		//term.display->fillRect(0, y, term.screen_width, lines * term->char_height, 0x0000);
	}
	uint16_t scroll_start = (t->scroll_start_row + t->scroll_value) * t->char_height;
	t->display->setScrollStart(scroll_start);

	/*
	int16_t pixels = lines * term->char_height;
	uint16_t scroll_min = t->top_margin * term->char_height;
	uint16_t scroll_max = t->bottom_margin * term->char_height;

	// starting position must be between top and bottom margin
	// scroll_start == top margin - no scroll at all
	if(t->scroll >= scroll_min){
		// clear the top n lines
		term.display->fillRect(0, t->scroll, term.screen_width, pixels, 0x0000);
		t->scroll += pixels;
	} else {
		term.display->fillRect(0, scroll_min, term.screen_width, pixels, 0x0000);
		t->scroll = scroll_min + pixels;
	}
	t->scroll = t->scroll % term.screen_height;
	term.display->setScrollStart(t->scroll);*/
}

// moves the cursor relative to current cursor position and scrolls the screen
void _vt100_move(struct vt100* term, int16_t right_left, int16_t bottom_top)
{
	// calculate how many lines we need to move down or up if x movement goes outside screen
	int16_t new_x = right_left + term->cursor_x;
	if(new_x > term->col_count) {
		if(term->flags.cursor_wrap) {
			bottom_top += new_x / term->col_count;
			term->cursor_x = new_x % term->col_count - 1;
		} else {
			term->cursor_x = term->col_count;
		}
	} else if(new_x < 0) {
		bottom_top += new_x / term->col_count - 1;
		term->cursor_x = term->col_count - (abs(new_x) % term->col_count) + 1;
	} else {
		term->cursor_x = new_x;
	}

	if(bottom_top) {
		int16_t new_y = term->cursor_y + bottom_top;
		int16_t to_scroll = 0;
		// bottom margin 39 marks last line as static on 40 line display
		// therefore, we would scroll when new cursor has moved to line 39
		// (or we could use new_y > term.row_count here
		// NOTE: new_y >= term->scroll_end_row ## to_scroll = (new_y - term->scroll_end_row) +1
		if(new_y >= term->scroll_end_row) {
			//scroll = new_y / term.row_count;
			//term->cursor_y = term.row_count;
			to_scroll = (new_y - term->scroll_end_row) + 1;
			// place cursor back within the scroll region
			term->cursor_y = term->scroll_end_row - 1; //new_y - to_scroll;
													   //scroll = new_y - term->bottom_margin;
													   //term->cursor_y = term->bottom_margin;
		} else if(new_y < term->scroll_start_row) {
			to_scroll = (new_y - term->scroll_start_row);
			term->cursor_y = term->scroll_start_row; //new_y - to_scroll;
													 //scroll = new_y / (term->bottom_margin - term->top_margin) - 1;
													 //term->cursor_y = term->top_margin;
		} else {
			// otherwise we move as normal inside the screen
			term->cursor_y = new_y;
		}
		_vt100_scroll(term, to_scroll);
	}
}

void _vt100_drawCursor(struct vt100* t)
{
	//uint16_t x = t->cursor_x * t->char_width;
	//uint16_t y = t->cursor_y * t->char_height;

	//term.display->fillRect(x, y, t->char_width, t->char_height, t->front_color);
}

// sends the character to the display and updates cursor position
void _vt100_putc(struct vt100* t, uint8_t ch)
{
	if(ch < 0x20 || ch > 0x7e) {
		static const char hex[] = "0123456789abcdef";
		_vt100_putc(t, '0');
		_vt100_putc(t, 'x');
		_vt100_putc(t, hex[((ch & 0xf0) >> 4)]);
		_vt100_putc(t, hex[(ch & 0x0f)]);
		return;
	}

	// calculate current cursor position in the display ram
	uint16_t x = VT100_CURSOR_X(t);
	uint16_t y = VT100_CURSOR_Y(t);

	term.display->setFrontColor(t->front_color);
	term.display->setBackColor(t->back_color);
	term.display->drawChar(x, y, ch);

	// move cursor right
	_vt100_move(t, 1, 0);
	_vt100_drawCursor(t);
}

STATE(_st_command_arg, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
		if(isdigit(arg)) { // a digit argument
			term->args[term->narg] = term->args[term->narg] * 10 + (arg - '0');
		} else if(arg == ';') { // separator
			term->narg++;
		} else { // no more arguments
			// go back to command state
			term->narg++;
			if(term->ret_state) {
				term->state = term->ret_state;
			} else {
				term->state = _st_idle;
			}
			// execute next state as well because we have already consumed a char!
			term->state(term, ev, arg);
		}
		break;
	}
	}
}

STATE(_st_esc_sq_bracket, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
		if(isdigit(arg)) { // start of an argument
			term->ret_state = _st_esc_sq_bracket;
			_st_command_arg(term, ev, arg);
			term->state = _st_command_arg;
		} else if(arg == ';') { // arg separator.
								// skip. And also stay in the command state
		} else {				// otherwise we execute the command and go back to idle
			switch(arg) {
			case 'A': { // move cursor up (cursor stops at top margin)
				int n = (term->narg > 0) ? term->args[0] : 1;
				term->cursor_y -= n;
				if(term->cursor_y < 0)
					term->cursor_y = 0;
				term->state = _st_idle;
				break;
			}
			case 'B': { // cursor down (cursor stops at bottom margin)
				int n = (term->narg > 0) ? term->args[0] : 1;
				term->cursor_y += n;
				if(term->cursor_y > term->row_count)
					term->cursor_y = term->row_count;
				term->state = _st_idle;
				break;
			}
			case 'C': { // cursor right (cursor stops at right margin)
				int n = (term->narg > 0) ? term->args[0] : 1;
				term->cursor_x += n;
				if(term->cursor_x > term->col_count)
					term->cursor_x = term->col_count;
				term->state = _st_idle;
				break;
			}
			case 'D': { // cursor left
				int n = (term->narg > 0) ? term->args[0] : 1;
				term->cursor_x -= n;
				if(term->cursor_x < 0)
					term->cursor_x = 0;
				term->state = _st_idle;
				break;
			}
			case 'f':
			case 'H': { // move cursor to position (default 0;0)
				// cursor stops at respective margins
				term->cursor_x = (term->narg >= 1) ? (term->args[1] - 1) : 0;
				term->cursor_y = (term->narg == 2) ? (term->args[0] - 1) : 0;
				if(term->flags.origin_mode) {
					term->cursor_y += term->scroll_start_row;
					if(term->cursor_y >= term->scroll_end_row) {
						term->cursor_y = term->scroll_end_row - 1;
					}
				}
				if(term->cursor_x > term->col_count)
					term->cursor_x = term->col_count;
				if(term->cursor_y > term->row_count)
					term->cursor_y = term->row_count;
				term->state = _st_idle;
				break;
			}
			case 'J': { // clear screen from cursor up or down
				uint16_t y = VT100_CURSOR_Y(term);
				if(term->narg == 0 || (term->narg == 1 && term->args[0] == 0)) {
					// clear down to the bottom of screen (including cursor)
					_vt100_clearLines(term, term->cursor_y, term->row_count);
				} else if(term->narg == 1 && term->args[0] == 1) {
					// clear top of screen to current line (including cursor)
					_vt100_clearLines(term, 0, term->cursor_y);
				} else if(term->narg == 1 && term->args[0] == 2) {
					// clear whole screen
					_vt100_clearLines(term, 0, term->row_count);
					// reset scroll value
					_vt100_resetScroll();
				}
				term->state = _st_idle;
				break;
			}
			case 'K': { // clear line from cursor right/left
				uint16_t x = VT100_CURSOR_X(term);
				uint16_t y = VT100_CURSOR_Y(term);

				if(term->narg == 0 || (term->narg == 1 && term->args[0] == 0)) {
					// clear to end of line (to \n or to edge?)
					// including cursor
					term->display->fillRect(x, y, term->screen_width - x, term->char_height, term->back_color);
				} else if(term->narg == 1 && term->args[0] == 1) {
					// clear from left to current cursor position
					term->display->fillRect(0, y, x + term->char_width, term->char_height, term->back_color);
				} else if(term->narg == 1 && term->args[0] == 2) {
					// clear whole current line
					term->display->fillRect(0, y, term->screen_width, term->char_height, term->back_color);
				}
				term->state = _st_idle;
				break;
			}

			case 'L': // insert lines (args[0] = number of lines)
			case 'M': // delete lines (args[0] = number of lines)
				term->state = _st_idle;
				break;
			case 'P': { // delete characters args[0] or 1 in front of cursor
				// TODO: this needs to correctly delete n chars
				int n = ((term->narg > 0) ? term->args[0] : 1);
				_vt100_move(term, -n, 0);
				for(int c = 0; c < n; c++) {
					_vt100_putc(term, ' ');
				}
				term->state = _st_idle;
				break;
			}
			case 'c': { // query device code
				term->callbacks->sendResponse("\e[?1;0c");
				term->state = _st_idle;
				break;
			}
			case 'x': {
				term->state = _st_idle;
				break;
			}
			case 's': { // save cursor pos
				term->saved_cursor_x = term->cursor_x;
				term->saved_cursor_y = term->cursor_y;
				term->state = _st_idle;
				break;
			}
			case 'u': { // restore cursor pos
				term->cursor_x = term->saved_cursor_x;
				term->cursor_y = term->saved_cursor_y;
				//_vt100_moveCursor(term, term->saved_cursor_x, term->saved_cursor_y);
				term->state = _st_idle;
				break;
			}
			case 'h':
			case 'l': {
				term->state = _st_idle;
				break;
			}

			case 'g': {
				term->state = _st_idle;
				break;
			}
			case 'm': { // sets colors. Accepts up to 3 args
				// [m means reset the colors to default
				if(!term->narg) {
					term->front_color = 0xffff;
					term->back_color = 0x0000;
				}
				while(term->narg) {
					term->narg--;
					int n = term->args[term->narg];
					static const uint16_t colors[] = {
						0x0000, // black
						0xf800, // red
						0x0780, // green
						0xfe00, // yellow
						0x001f, // blue
						0xf81f, // magenta
						0x07ff, // cyan
						0xffff  // white
					};
					if(n == 0) { // all attributes off
						term->front_color = 0xffff;
						term->back_color = 0x0000;

						term->display->setFrontColor(term->front_color);
						term->display->setBackColor(term->back_color);
					}
					if(n >= 30 && n < 38) { // fg colors
						term->front_color = colors[n - 30];
						term->display->setFrontColor(term->front_color);
					} else if(n >= 40 && n < 48) {
						term->back_color = colors[n - 40];
						term->display->setBackColor(term->back_color);
					}
				}
				term->state = _st_idle;
				break;
			}

			case '@': // Insert Characters
				term->state = _st_idle;
				break;
			case 'r': // Set scroll region (top and bottom margins)
				// the top value is first row of scroll region
				// the bottom value is the first row of static region after scroll
				if(term->narg == 2 && term->args[0] < term->args[1]) {
					// [1;40r means scroll region between 8 and 312
					// bottom margin is 320 - (40 - 1) * 8 = 8 pix
					term->scroll_start_row = term->args[0] - 1;
					term->scroll_end_row = term->args[1] - 1;
					uint16_t top_margin = term->scroll_start_row * term->char_height;
					uint16_t bottom_margin = term->screen_height - (term->scroll_end_row * term->char_height);
					term->display->setScrollMargins(top_margin, bottom_margin);
					//term.display->setScrollStart(0); // reset scroll
				} else {
					_vt100_resetScroll();
				}
				term->state = _st_idle;
				break;
			case 'i':   // Printing
			case 'y':   // self test modes..
			case '=': { // argument follows...
				//term->state = _st_screen_mode;
				term->state = _st_idle;
				break;
			}
			case '?': // '[?' escape mode
				term->state = _st_esc_question;
				break;
			default: { // unknown sequence

				term->state = _st_idle;
				break;
			}
			}
			//term->state = _st_idle;
		} // else
		break;
	}
	default: { // switch (ev)
		// for all other events restore normal mode
		term->state = _st_idle;
	}
	}
}

STATE(_st_esc_question, term, ev, arg)
{
	// DEC mode commands
	switch(ev) {
	case EV_CHAR: {
		if(isdigit(arg)) { // start of an argument
			term->ret_state = _st_esc_question;
			_st_command_arg(term, ev, arg);
			term->state = _st_command_arg;
		} else if(arg == ';') { // arg separator.
								// skip. And also stay in the command state
		} else {
			switch(arg) {
			case 'l':
				// dec mode: OFF (arg[0] = function)
			case 'h': {
				// dec mode: ON (arg[0] = function)
				switch(term->args[0]) {
				case 1: { // cursor keys mode
					// h = esc 0 A for cursor up
					// l = cursor keys send ansi commands
					break;
				}
				case 2: { // ansi / vt52
					// h = ansi mode
					// l = vt52 mode
					break;
				}
				case 3: {
					// h = 132 chars per line
					// l = 80 chars per line
					break;
				}
				case 4: {
					// h = smooth scroll
					// l = jump scroll
					break;
				}
				case 5: {
					// h = black on white bg
					// l = white on black bg
					break;
				}
				case 6: {
					// h = cursor relative to scroll region
					// l = cursor independent of scroll region
					term->flags.origin_mode = (arg == 'h') ? 1 : 0;
					break;
				}
				case 7: {
					// h = new line after last column
					// l = cursor stays at the end of line
					term->flags.cursor_wrap = (arg == 'h') ? 1 : 0;
					break;
				}
				case 8: {
					// h = keys will auto repeat
					// l = keys do not auto repeat when held down
					break;
				}
				case 9: {
					// h = display interlaced
					// l = display not interlaced
					break;
				}
					// 10-38 - all quite DEC speciffic commands so omitted here
				}
				term->state = _st_idle;
				break;
			}
			case 'i': /* Printing */
			case 'n': /* Request printer status */
			default:
				term->state = _st_idle;
				break;
			}
			term->state = _st_idle;
		}
	}
	}
}

STATE(_st_esc_left_br, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
		switch(arg) {
		case 'A':
		case 'B':
			// translation map command?
		case '0':
		case 'O':
			// another translation map command?
			term->state = _st_idle;
			break;
		default:
			term->state = _st_idle;
		}
		//term->state = _st_idle;
	}
	}
}

STATE(_st_esc_right_br, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
		switch(arg) {
		case 'A':
		case 'B':
			// translation map command?
		case '0':
		case 'O':
			// another translation map command?
			term->state = _st_idle;
			break;
		default:
			term->state = _st_idle;
		}
		//term->state = _st_idle;
	}
	}
}

STATE(_st_esc_hash, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
		switch(arg) {
		case '8': {
			// self test: fill the screen with 'E'

			term->state = _st_idle;
			break;
		}
		default:
			term->state = _st_idle;
		}
	}
	}
}

STATE(_st_escape, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
#define CLEAR_ARGS                                                                                                     \
	{                                                                                                                  \
		term->narg = 0;                                                                                                \
		for(int c = 0; c < MAX_COMMAND_ARGS; c++)                                                                      \
			term->args[c] = 0;                                                                                         \
	}

		switch(arg) {
		case '[': { // command
			// prepare command state and switch to it
			CLEAR_ARGS;
			term->state = _st_esc_sq_bracket;
			break;
		}
		case '(': /* ESC ( */
			CLEAR_ARGS;
			term->state = _st_esc_left_br;
			break;
		case ')': /* ESC ) */
			CLEAR_ARGS;
			term->state = _st_esc_right_br;
			break;
		case '#': // ESC #
			CLEAR_ARGS;
			term->state = _st_esc_hash;
			break;
		case 'P': //ESC P (DCS, Device Control String)
			term->state = _st_idle;
			break;
		case 'D': // moves cursor down one line and scrolls if necessary
			// move cursor down one line and scroll window if at bottom line
			_vt100_move(term, 0, 1);
			term->state = _st_idle;
			break;
		case 'M': // Cursor up
			// move cursor up one line and scroll window if at top line
			_vt100_move(term, 0, -1);
			term->state = _st_idle;
			break;
		case 'E': // next line
			// same as '\r\n'
			_vt100_move(term, 0, 1);
			term->cursor_x = 0;
			term->state = _st_idle;
			break;
		case '7': // Save attributes and cursor position
		case 's':
			term->saved_cursor_x = term->cursor_x;
			term->saved_cursor_y = term->cursor_y;
			term->state = _st_idle;
			break;
		case '8': // Restore them
		case 'u':
			term->cursor_x = term->saved_cursor_x;
			term->cursor_y = term->saved_cursor_y;
			term->state = _st_idle;
			break;
		case '=': // Keypad into applications mode
			term->state = _st_idle;
			break;
		case '>': // Keypad into numeric mode
			term->state = _st_idle;
			break;
		case 'Z': // Report terminal type
			// vt 100 response
			term->callbacks->sendResponse("\033[?1;0c");
			// unknown terminal
			//out("\033[?c");
			term->state = _st_idle;
			break;
		case 'c': // Reset terminal to initial state
			_vt100_reset();
			term->state = _st_idle;
			break;
		case 'H': // Set tab in current position
		case 'N': // G2 character set for next character only
		case 'O': // G3 "               "
		case '<': // Exit vt52 mode
			// ignore
			term->state = _st_idle;
			break;
		case KEY_ESC: { // marks start of next escape sequence
			// stay in escape state
			break;
		}
		default: { // unknown sequence - return to normal mode
			term->state = _st_idle;
			break;
		}
		}
#undef CLEAR_ARGS
		break;
	}
	default: {
		// for all other events restore normal mode
		term->state = _st_idle;
	}
	}
}

STATE(_st_idle, term, ev, arg)
{
	switch(ev) {
	case EV_CHAR: {
		switch(arg) {
		case 5:									// AnswerBack for vt100's
			term->callbacks->sendResponse("X"); // should send SCCS_ID?
			break;
		case '\n': { // new line
			_vt100_move(term, 0, 1);
			term->cursor_x = 0;
			//_vt100_moveCursor(term, 0, term->cursor_y + 1);
			// do scrolling here!
			break;
		}
		case '\r': { // carrage return (0x0d)
			term->cursor_x = 0;
			//_vt100_move(term, 0, 1);
			//_vt100_moveCursor(term, 0, term->cursor_y);
			break;
		}
		case '\b': { // backspace 0x08
			_vt100_move(term, -1, 0);
			// backspace does not delete the character! Only moves cursor!
			//term.display->drawChar(term->cursor_x * term->char_width,
			//	term->cursor_y * term->char_height, ' ');
			break;
		}
		case KEY_DEL: { // del - delete character under cursor
			// Problem: with current implementation, we can't move the rest of line
			// to the left as is the proper behavior of the delete character
			// fill the current position with background color
			_vt100_putc(term, ' ');
			_vt100_move(term, -1, 0);
			//_vt100_clearChar(term, term->cursor_x, term->cursor_y);
			break;
		}
		case '\t': { // tab
			// tab fills characters on the line until we reach a multiple of tab_stop
			int tab_stop = 4;
			int to_put = tab_stop - (term->cursor_x % tab_stop);
			while(to_put--)
				_vt100_putc(term, ' ');
			break;
		}
		case KEY_BELL: { // bell is sent by bash for ex. when doing tab completion
			// sound the speaker bell?
			// skip
			break;
		}
		case KEY_ESC: { // escape
			term->state = _st_escape;
			break;
		}
		default: {
			_vt100_putc(term, arg);
			break;
		}
		}
		break;
	}
	default: {
	}
	}
}

void vt100_init(DisplayDevice* display, VT100Callbacks* callbacks)
{
	term.display = display;
	term.callbacks = callbacks;
	_vt100_reset();
}

void vt100_putc(uint8_t c)
{
	/*char *buffer = 0; 
	switch(c){
		case KEY_UP:         buffer="\e[A";    break;
		case KEY_DOWN:       buffer="\e[B";    break;
		case KEY_RIGHT:      buffer="\e[C";    break;
		case KEY_LEFT:       buffer="\e[D";    break;
		case KEY_BACKSPACE:  buffer="\b";      break;
		case KEY_IC:         buffer="\e[2~";   break;
		case KEY_DC:         buffer="\e[3~";   break;
		case KEY_HOME:       buffer="\e[7~";   break;
		case KEY_END:        buffer="\e[8~";   break;
		case KEY_PPAGE:      buffer="\e[5~";   break;
		case KEY_NPAGE:      buffer="\e[6~";   break;
		case KEY_SUSPEND:    buffer="\x1A";    break;      // ctrl-z
		case KEY_F(1):       buffer="\e[[A";   break;
		case KEY_F(2):       buffer="\e[[B";   break;
		case KEY_F(3):       buffer="\e[[C";   break;
		case KEY_F(4):       buffer="\e[[D";   break;
		case KEY_F(5):       buffer="\e[[E";   break;
		case KEY_F(6):       buffer="\e[17~";  break;
		case KEY_F(7):       buffer="\e[18~";  break;
		case KEY_F(8):       buffer="\e[19~";  break;
		case KEY_F(9):       buffer="\e[20~";  break;
		case KEY_F(10):      buffer="\e[21~";  break;
	}
	if(buffer){
		while(*buffer){
			term.state(&term, EV_CHAR, *buffer++);
		}
	} else {
		term.state(&term, EV_CHAR, 0x0000 | c);
	}*/
	term.state(&term, EV_CHAR, 0x0000 | c);
}

void vt100_puts(const char* str)
{
	while(*str) {
		vt100_putc(*str++);
	}
}

size_t vt100_nputs(const char* str, size_t length)
{
	unsigned n = length;
	while(n--) {
		vt100_putc(*str++);
	}
	return length;
}
