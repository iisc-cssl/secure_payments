#include <stdint.h>

#include <ta_pio.h>
#include <tee_api.h>
#include <trace.h>
#include <pta_tui.h>

static TEE_TASessionHandle pioSTA = TEE_HANDLE_NULL;
static const TEE_UUID tui_uuid = PTA_TUI_UUID;

TEE_Result pio_display_msg(void) {
    TEE_Result	res = TEE_SUCCESS;

	TEE_Param session_params[TEE_NUM_PARAMS];
    TEE_Param invoke_params[TEE_NUM_PARAMS];
    uint32_t session_param_types = TEE_PARAM_TYPES(
                            TEE_PARAM_TYPE_NONE,
						    TEE_PARAM_TYPE_NONE,
						    TEE_PARAM_TYPE_NONE,
						    TEE_PARAM_TYPE_NONE);
    uint32_t invoke_param_types = TEE_PARAM_TYPES(
                            TEE_PARAM_TYPE_MEMREF_INPUT,
                            TEE_PARAM_TYPE_NONE,
                            TEE_PARAM_TYPE_NONE,
                            TEE_PARAM_TYPE_NONE);

    res = TEE_OpenTASession(&tui_uuid, TEE_TIMEOUT_INFINITE,
			session_param_types, session_params, &pioSTA, NULL);
	if (res == TEE_ERROR_BUSY) {
		EMSG("TUI TA is busy");
        return res;
	} else if (res != TEE_SUCCESS) {
		EMSG("Failed to create session with TUI static TA (%x)", res);
        return res;
	}
    TUI_MessageForUser *tuiMsg = (TUI_MessageForUser *)malloc(sizeof(TUI_MessageForUser));
    tuiMsg->confirmationRequired = false;
    tuiMsg->msg = "Test message for display";
    tuiMsg->msg_length = strlen(tuiMsg->msg);
    tuiMsg->url = "securepayments.com";
    tuiMsg->url_length = strlen(tuiMsg->url);

    invoke_params[0].memref.buffer = tuiMsg;
    invoke_params[0].memref.size = sizeof(TUI_MessageForUser);

    res = TEE_InvokeTACommand(pioSTA, TEE_TIMEOUT_INFINITE,
			PTA_TUI_DISPLAY_MSG,
			invoke_param_types, invoke_params, NULL);
    if(res == TEE_SUCCESS) {
        DMSG("Successful display of message to user");
    } else {
        EMSG("Failed to display message to user");
    }
    TEE_CloseTASession(pioSTA);
    return res;
}
TEE_Result pio_get_confirmation(void) {
    TEE_Result	res = TEE_SUCCESS;

	TEE_Param session_params[TEE_NUM_PARAMS];
    TEE_Param invoke_params[TEE_NUM_PARAMS];
    uint32_t session_param_types = TEE_PARAM_TYPES(
                            TEE_PARAM_TYPE_NONE,
						    TEE_PARAM_TYPE_NONE,
						    TEE_PARAM_TYPE_NONE,
						    TEE_PARAM_TYPE_NONE);
    uint32_t invoke_param_types = TEE_PARAM_TYPES(
                            TEE_PARAM_TYPE_MEMREF_INOUT,
                            TEE_PARAM_TYPE_NONE,
                            TEE_PARAM_TYPE_NONE,
                            TEE_PARAM_TYPE_NONE);

    res = TEE_OpenTASession(&tui_uuid, TEE_TIMEOUT_INFINITE,
			session_param_types, session_params, &pioSTA, NULL);
	if (res == TEE_ERROR_BUSY) {
		EMSG("TUI TA is busy");
        return res;
	} else if (res != TEE_SUCCESS) {
		EMSG("Failed to create session with TUI static TA (%x)", res);
        return res;
	}
    TUI_MessageForUser *tuiMsg = (TUI_MessageForUser *)malloc(sizeof(TUI_MessageForUser));
    tuiMsg->confirmationRequired = true;
    tuiMsg->msg = "Test message for confirmation";
    tuiMsg->msg_length = strlen(tuiMsg->msg);
    tuiMsg->url = "securepayments.com";
    tuiMsg->url_length = strlen(tuiMsg->url);

    invoke_params[0].memref.buffer = tuiMsg;
    invoke_params[0].memref.size = sizeof(TUI_MessageForUser);

    res = TEE_InvokeTACommand(pioSTA, TEE_TIMEOUT_INFINITE,
			PTA_TUI_DISPLAY_MSG_FOR_CONFIRMATION,
			invoke_param_types, invoke_params, NULL);
    if(res == TEE_SUCCESS) {
        if(((TUI_MessageForUser *)(invoke_params[0].memref.buffer))->confirmationGiven) {
            DMSG("User gave confirmation");
        } else {
            DMSG("User declined confirmation");
        }
    } else {
        EMSG("Failed to get confirmation from user");
    }
    TEE_CloseTASession(pioSTA);
    return res;
}
TEE_Result pio_get_user_input(void) {
    TEE_Result	res = TEE_SUCCESS;

	TEE_Param session_params[TEE_NUM_PARAMS];
    TEE_Param invoke_params[TEE_NUM_PARAMS];
    uint32_t session_param_types = TEE_PARAM_TYPES(
                            TEE_PARAM_TYPE_NONE,
						    TEE_PARAM_TYPE_NONE,
						    TEE_PARAM_TYPE_NONE,
						    TEE_PARAM_TYPE_NONE);
    uint32_t invoke_param_types = TEE_PARAM_TYPES(
                            TEE_PARAM_TYPE_MEMREF_INOUT,
                            TEE_PARAM_TYPE_NONE,
                            TEE_PARAM_TYPE_NONE,
                            TEE_PARAM_TYPE_NONE);

    res = TEE_OpenTASession(&tui_uuid, TEE_TIMEOUT_INFINITE,
			session_param_types, session_params, &pioSTA, NULL);
	if (res == TEE_ERROR_BUSY) {
		EMSG("TUI TA is busy");
        return res;
	} else if (res != TEE_SUCCESS) {
		EMSG("Failed to create session with TUI static TA (%x)", res);
        return res;
	}

    TUI_TextEntryField *field1 = (TUI_TextEntryField *)malloc(sizeof(TUI_TextEntryField));
    field1->base.description = "Enter username containing 5-60 characters";
    field1->base.description_length = strlen(field1->base.description);
    field1->base.label = "Username";
    field1->base.label_length = strlen(field1->base.label);
    field1->has_default_value = false;
    field1->max_length = 60;
    field1->min_length = 5;
    field1->value = (char *)malloc(sizeof(char) * (MAX_TEXT_FIELD_VALUE_LENGTH + 1));
    field1->value[0] = '\0';
    field1->value_length = 0;

    TUI_PasswordEntryField *field2 = (TUI_PasswordEntryField *)malloc(sizeof(TUI_PasswordEntryField));
    field2->base.description = "Password needs to be between 8 and 16 characters in length and must have "
            "at least one characters each from i) A-Z ii) a-z iii) 0-9 iv) {@, $, _, #}";
    field2->base.description_length = strlen(field2->base.description);
    field2->base.label = "Password";
    field2->base.label_length = strlen(field2->base.label);
    field2->value = (char *)malloc(sizeof(char) * (MAX_PASSWORD_FIELD_VALUE_LENGTH + 1));
    field2->value[0] = '\0';
    field2->value_length = 0;
    field2->max_length = 16;
    field2->min_length = 8;

    TUI_TextEntryField *field3 = (TUI_TextEntryField *)malloc(sizeof(TUI_TextEntryField));
    field3->base.description = "Enter your Aadhaar number.";
    field3->base.description_length = strlen(field3->base.description);
    field3->base.label = "Aadhaar Number";
    field3->base.label_length = strlen(field3->base.label);
    field3->has_default_value = false;
    field3->max_length = 12;
    field3->min_length = 12;
    field3->value = (char *)malloc(sizeof(char) * (MAX_TEXT_FIELD_VALUE_LENGTH + 1));
    field3->value[0] = '\0';
    field3->value_length = 0;

    TUI_UserInputForm *input_form = (TUI_UserInputForm *)malloc(sizeof(TUI_UserInputForm));
    input_form->description = "Form for registering new user";
    input_form->description_length = strlen(input_form->description);
    input_form->title = "Sign Up";
    input_form->url = "securepayments.com";

    input_form->field_count = 3;
    input_form->fields = (TUI_EntryFieldWrapper *)malloc(sizeof(TUI_EntryFieldWrapper) * input_form->field_count);

    input_form->fields[0].type = TUI_ENTRY_FIELD_TEXT;
    input_form->fields[0].field_ptr = field1;

    input_form->fields[1].type = TUI_ENTRY_FIELD_PASSWORD;
    input_form->fields[1].field_ptr = field2;

    input_form->fields[2].type = TUI_ENTRY_FIELD_TEXT;
    input_form->fields[2].field_ptr = field3;

    invoke_params[0].memref.buffer = input_form;
    invoke_params[0].memref.size = sizeof(TUI_UserInputForm);

    res = TEE_InvokeTACommand(pioSTA, TEE_TIMEOUT_INFINITE,
			PTA_TUI_GET_USER_INPUT,
			invoke_param_types, invoke_params, NULL);
    if(res == TEE_SUCCESS) {
        input_form = (TUI_UserInputForm *)(invoke_params[0].memref.buffer);
        if(input_form->formSubmitted) {
            DMSG("User submitted form");
            field1 = (TUI_TextEntryField *)(input_form->fields[0].field_ptr);
            DMSG("The username entered by the user : %s",
                    field1->value);
            field2 = (TUI_PasswordEntryField *)(input_form->fields[1].field_ptr);
            DMSG("The password entered by the user : %s",
                    field2->value);
        } else {
            DMSG("User canceled the form");
        }
    } else {
        EMSG("Failed to show input form to user");
    }
    TEE_CloseTASession(pioSTA);

   return res;
}
