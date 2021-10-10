#include <tee_ta_api.h>
#include <user_ta_header_defines.h>
#include <ta_pio.h>



/* Called each time a new instance is created */
TEE_Result TA_CreateEntryPoint(void)
{
	return TEE_SUCCESS;
}

/* Called each time an instance is destroyed */
void TA_DestroyEntryPoint(void)
{
}

/* Called each time a session is opened */
TEE_Result TA_OpenSessionEntryPoint(uint32_t nParamTypes, TEE_Param pParams[4],
				    void **ppSessionContext)
{
	(void)nParamTypes;
	(void)pParams;
	(void)ppSessionContext;
	return TEE_SUCCESS;
}

/* Called each time a session is closed */
void TA_CloseSessionEntryPoint(void *pSessionContext)
{
	(void)pSessionContext;
}

/* Called when a command is invoked */
TEE_Result TA_InvokeCommandEntryPoint(void *pSessionContext,
				      uint32_t nCommandID, uint32_t nParamTypes,
				      TEE_Param pParams[4])
{
	(void)pSessionContext;
	(void)nParamTypes;
	(void)pParams;

	switch (nCommandID) {
	case TA_PIO_CMD_TEST_DISPLAY:
		return pio_display_msg();

	case TA_PIO_CMD_TEST_CONFIRMATION:
		return pio_get_confirmation();

	case TA_PIO_CMD_TEST_FORM_INPUT:
		return pio_get_user_input();

	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
}
