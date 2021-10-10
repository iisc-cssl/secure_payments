#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "xtest_test.h"
#include "xtest_helpers.h"
#include <signed_hdr.h>
#include <util.h>

#include <pta_invoke_tests.h>
#include <ta_pio_test.h>

static void xtest_tee_test_50001(ADBG_Case_t *c)
{
	TEEC_Result res;
	TEEC_Session session = { 0 };
	uint32_t ret_orig;

	res = xtest_teec_open_session(&session, &pio_test_ta_uuid, NULL,
				      &ret_orig);

	ADBG_EXPECT_TEEC_SUCCESS(c, res);

    Do_ADBG_BeginSubCase(c, "TUI Test Display Message");
	res = TEEC_InvokeCommand(&session, TA_PIO_CMD_TEST_DISPLAY,
				 NULL, &ret_orig);
	ADBG_EXPECT_TEEC_SUCCESS(c, res);
    Do_ADBG_EndSubCase(c, "TUI Test Display Message");

    Do_ADBG_BeginSubCase(c, "TUI Test Get Confirmation");
	res = TEEC_InvokeCommand(&session, TA_PIO_CMD_TEST_CONFIRMATION,
				 NULL, &ret_orig);
	ADBG_EXPECT_TEEC_SUCCESS(c, res);
    Do_ADBG_EndSubCase(c, "TUI Test Get Confirmation");

    Do_ADBG_BeginSubCase(c, "TUI Test Get User Input");
	res = TEEC_InvokeCommand(&session, TA_PIO_CMD_TEST_FORM_INPUT,
				 NULL, &ret_orig);
	ADBG_EXPECT_TEEC_SUCCESS(c, res);
    Do_ADBG_EndSubCase(c, "TUI Test Get User Input");

	TEEC_CloseSession(&session);
}
ADBG_CASE_DEFINE(tui, 50001, xtest_tee_test_50001,
		"TUI test");