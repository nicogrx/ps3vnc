#include <SDL/SDL.h>
#include "sysutil/sysutil.h"
#include "sysutil/msg.h"
#include "screen.h"

enum dialog_actions {
	DIALOG_ACTION_NONE=-1,
	DIALOG_ACTION_INIT=0,
	DIALOG_ACTION_OK,
	DIALOG_ACTION_NO,
};

static volatile int dialog_action = DIALOG_ACTION_INIT;
static void my_dialog(msgButton button, void *userdata)
{
	switch(button) {
        case MSG_DIALOG_BTN_OK:
		dialog_action = DIALOG_ACTION_OK;
		break;
        case MSG_DIALOG_BTN_NO:
        case MSG_DIALOG_BTN_ESCAPE:
		dialog_action = DIALOG_ACTION_NO;
		break;
        case MSG_DIALOG_BTN_NONE:
		dialog_action = DIALOG_ACTION_NONE;
		break;
        default:
		break;
	}
}

void ok_dialog(char *text, int ok)
{
	msgType mdialogok = MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_OK;  
	if(ok == 1)
		msgDialogOpen2(mdialogok, text, my_dialog, (void *) 0x22220001, NULL);
	else
		msgDialogOpen2(mdialogok, text, my_dialog, (void *) 0x22220002, NULL);
		dialog_action = 0;
	while(!dialog_action)
	{
		sysUtilCheckCallback();
		updateDisplay();
	}
	msgDialogClose(0);
}

int yes_dialog(char * text)
{
	msgType mdialogyesno = MSG_DIALOG_NORMAL | MSG_DIALOG_BTN_TYPE_YESNO |
		MSG_DIALOG_DISABLE_CANCEL_ON | MSG_DIALOG_DEFAULT_CURSOR_NO;
	msgDialogOpen2(mdialogyesno, text, my_dialog, (void *) 0x11110001, NULL);
	dialog_action = 0;
	while(dialog_action == DIALOG_ACTION_INIT)
	{
		sysUtilCheckCallback();
		updateDisplay();
	}
	msgDialogClose(0);
	if (dialog_action != DIALOG_ACTION_OK)
		return 0;
	else
		return 1;
}
