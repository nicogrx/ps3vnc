#include "keymap.h"

#define XK_MISCELLANY
#define XK_XKB_KEYS
#define XK_LATIN1
#include "keysymdef.h"

int map_key(int key, unsigned char downflag)
{
	static int shifted = 0;
	static int altgr = 0;
	int out_key = 0;
	
	switch(key) {
	case 0x8:
		out_key = XK_BackSpace;
		break;
	case 0x9:	
		out_key = XK_Tab;
		break;
	case 0xd:
		out_key = XK_Return;
		break;
	case 0x4000003a:
		out_key = XK_F1;
		break;
	case 0x4000003b:
		out_key = XK_F2;
		break;
	case 0x4000003c:
		out_key = XK_F3;
		break;
	case 0x4000003d:
		out_key = XK_F4;
		break;
	case 0x4000003e:
		out_key = XK_F5;
		break;
	case 0x4000003f:
		out_key = XK_F6;
		break;
	case 0x40000040:
		out_key = XK_F7;
		break;
	case 0x40000041:
		out_key = XK_F8;
		break;
	case 0x40000042:
		out_key = XK_F9;
		break;
	case 0x40000043:
		out_key = XK_F10;
		break;
	case 0x40000044:
		out_key = XK_F11;
		break;
	case 0x40000045:
		out_key = XK_F12;
		break;
	case 0x400000e1:
		if (downflag)
			shifted = 1;
		else
			shifted = 0;
		out_key = XK_Shift_L;
		break;
	case 0x400000e5:
		shifted = downflag ? 1: 0;
		out_key = XK_Shift_R;
		break;
	case 0x400000e0:
		out_key = XK_Control_L;
		break;
	case 0x400000e4:
		out_key = XK_Control_R;
		break;
	case 0x400000e6:
		altgr = downflag ? 1: 0;
		//out_key = XK_Alt_R;
		out_key = -1;
		break;
	case 0x40000039:
		out_key = XK_Caps_Lock;
		break;
	case 0x400000e2:
		out_key = XK_Alt_L;
		break;
	case 0x40000050:
		out_key = XK_Left;
		break;
	case 0x40000052:
		out_key = XK_Up;
		break;
	case 0x4000004f:
		out_key = XK_Right;
		break;
	case 0x40000051:
		out_key = XK_Down;
		break;
	case 0x4000004a:
		out_key = XK_Home;
		break;
	case 0x4000004b:
		out_key = XK_Page_Up;
		break;
	case 0x4000004d:
		out_key = XK_End;
		break;
	case 0x4000004e:
		out_key = XK_Page_Down;
		break;
	case 0x1b:
		out_key = XK_Escape;
		break;
	case XK_ampersand:
		if (shifted)
			out_key = XK_1;
		else
			out_key = key;
		break;
	case XK_eacute:
		if (shifted)
			out_key = XK_2;
		else if (altgr)
			out_key = XK_asciitilde;
		else
			out_key = key;
		break;
	case XK_quotedbl:
		if (shifted)
			out_key = XK_3;
		else if (altgr)
			out_key = XK_numbersign;
		else
			out_key = key;
		break;
	case XK_apostrophe:
		if (shifted)
			out_key = XK_4;
		else if (altgr)
			out_key = XK_braceleft;
		else
			out_key = key;
		break;
	case XK_parenleft:
		if (shifted)
			out_key = XK_5;
		else if (altgr)
			out_key = XK_bracketleft;
		else
			out_key = key;
		break;
	case XK_minus:
		if (shifted)
			out_key = XK_6;
		else if (altgr)
			out_key = XK_bar;
		else
			out_key = key;
		break;
	case XK_egrave:
		if (shifted)
			out_key = XK_7;
		else if (altgr)
			out_key = XK_grave;
		else
			out_key = key;
		break;
	case XK_underscore:
		if (shifted)
			out_key = XK_8;
		else if (altgr)
			out_key = XK_backslash;
		else
			out_key = key;
		break;
	case XK_ccedilla:
		if (shifted)
			out_key = XK_9;
		else if (altgr)
			out_key = XK_asciicircum;
		else
			out_key = key;
		break;
	case XK_agrave:
		if (shifted)
			out_key = XK_0;
		else if (altgr)
			out_key = XK_at;
		else
			out_key = key;
		break;
	case XK_parenright:
		if (shifted)
			out_key = XK_degree;
		else if (altgr)
			out_key = XK_bracketright;
		else
			out_key = key;
		break;
	case XK_equal:
		if (shifted)
			out_key = XK_plus;
		else if (altgr)
			out_key = XK_braceright;
		else
			out_key = key;
		break;
	case XK_comma:
		if (shifted)
			out_key = XK_question;
		else
			out_key = key;
		break;
	case XK_semicolon:
		if (shifted)
			out_key = XK_period;
		else
			out_key = key;
		break;
	case XK_colon:
		if (shifted)
			out_key = XK_slash;
		else
			out_key = key;
		break;
	case XK_exclam:
		if (shifted)
			out_key = XK_section;
		else
			out_key = key;
		break;
	case XK_ugrave:
		if (shifted)
			out_key = XK_percent;
		else
			out_key = key;
		break;
	case XK_asterisk:
		if (shifted)
			out_key = XK_mu;
		else
			out_key = key;
		break;
	case XK_dead_circumflex:
		if (shifted)
			out_key = XK_dead_diaeresis;
		else
			out_key = key;
		break;
	case XK_dollar:
		if (shifted)
			out_key = XK_sterling;
		else
			out_key = key;
		break;

	default:
		if (key >= 0x61 && key <= 0x7a) {
			if (shifted)
				out_key = key - 0x20;
			else
				out_key = key;
		} else
			out_key = key;
		break;	
	}
	return out_key;	
}
