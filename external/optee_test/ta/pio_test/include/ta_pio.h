#ifndef TA_PIO_H
#define TA_PIO_H

#include <ta_pio_test.h>
#include <tee_api.h>


//Commands for TUI PTA
#define PTA_TUI_DISPLAY_MSG 1
#define PTA_TUI_DISPLAY_MSG_FOR_CONFIRMATION 2
#define PTA_TUI_GET_USER_INPUT 3


TEE_Result pio_display_msg(void);
TEE_Result pio_get_confirmation(void);
TEE_Result pio_get_user_input(void);

#endif // TA_PIO_H
