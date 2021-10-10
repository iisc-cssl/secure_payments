#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ta_aes_perf.h>
#include <tee_client_api.h>
#include <tee_client_api_extensions.h>
#include <time.h>
#include <unistd.h>

#include "crypto_common.h"
#include "xtest_helpers.h"

#define _TO_STR(x) #x
#define TO_STR(x) _TO_STR(x)


static void errx(const char *msg, TEEC_Result res, uint32_t *orig)
{
	fprintf(stderr, "%s: 0x%08x", msg, res);
	if (orig)
		fprintf(stderr, " (orig=%d)", (int)*orig);
	fprintf(stderr, "\n");
	exit (1);
}

static void check_res(TEEC_Result res, const char *errmsg, uint32_t *orig)
{
	if (res != TEEC_SUCCESS)
		errx(errmsg, res, orig);
}

static void get_current_time(struct timespec *ts)
{
	if (clock_gettime(CLOCK_MONOTONIC, ts) < 0) {
		perror("clock_gettime");
		exit(1);
	}
}

static uint64_t timespec_to_ns(struct timespec *ts)
{
	return ((uint64_t)ts->tv_sec * 1000000000) + ts->tv_nsec;
}

static uint64_t timespec_diff_ns(struct timespec *start, struct timespec *end)
{
	return timespec_to_ns(end) - timespec_to_ns(start);
}



#define TA_KEYMASTER_UUID { 0xdba51a17, 0x0563, 0x11e7, \
		{ 0x93, 0xb1, 0x6f, 0xa7, 0xb0, 0x07, 0x1a, 0x51} }
#define PIO_BUF_SIZE 4096

static TEEC_Context pio_ctx;
static TEEC_Session pio_sess;


static void dump_line(uint8_t const *line_start, uint32_t line_len) {
        char *line_buf = (char *)malloc(line_len * 2 + 1);
        memset(line_buf, 0, line_len * 2 + 1);
        for(uint32_t i = 0; i < line_len; i++) {
                snprintf(&line_buf[i * 2], 3, "%02x", line_start[i]);
        }
        printf("%s\n", line_buf);
		free(line_buf);
}

/*static void dump_binary(char const *start, size_t len) {
        size_t i = 0;
        while(len != 0) {
                if(len <= 8) {
                        dump_line(&start[i], len);
                        len = 0;
                } else {
                        dump_line(&start[i], 8);
                        i += 8;
                        len -= 8;
                }
        }
}*/

static int pio_test_run(unsigned char *input_data, size_t input_data_len, uint32_t cmd) {
	TEEC_Result res = TEEC_ERROR_GENERIC;
	res = TEEC_InitializeContext(NULL, &pio_ctx);
	check_res(res, "TEEC_InitializeContext", NULL);

	TEEC_Operation op = TEEC_OPERATION_INITIALIZER;
	TEEC_UUID uuid = TA_KEYMASTER_UUID;
	uint32_t err_origin = 0;

    struct timespec t0 = { };
    struct timespec t1 = { };

	
	uint8_t output_data[PIO_BUF_SIZE];
	memset(output_data, 0, PIO_BUF_SIZE);

	res = TEEC_OpenSession(&pio_ctx, &pio_sess, &uuid, TEEC_LOGIN_PUBLIC, NULL,
			       NULL, &err_origin);
	check_res(res, "TEEC_OpenSession", &err_origin);

    op.paramTypes = (uint32_t)TEEC_PARAM_TYPES(TEEC_MEMREF_TEMP_INPUT,
					       TEEC_MEMREF_TEMP_OUTPUT,
					       TEEC_NONE,
					       TEEC_NONE);

	op.params[0].tmpref.buffer = (void*)input_data;
	op.params[0].tmpref.size   = (uint32_t)input_data_len;
	op.params[1].tmpref.buffer = (void*)output_data;
	op.params[1].tmpref.size   = PIO_BUF_SIZE;

	get_current_time(&t0);
	fprintf(stderr, "About to invoke PIO TA\n");
	res = TEEC_InvokeCommand(&pio_sess, cmd, &op, &err_origin);
	get_current_time(&t1);
	fprintf(stderr, "PIO TA returned 0x%x\n", res);

	uint64_t elapsed_time = timespec_diff_ns(&t0, &t1);

	fprintf(stdout, "\n%" PRIX32 " %" PRIu64 " %" PRIu64 " ", res, elapsed_time, op.params[1].tmpref.size);
	dump_line(
			output_data, 
			(op.params[1].tmpref.size < 13 ? 13 : op.params[1].tmpref.size)
	);
	fprintf(stdout, "\n");

    TEEC_CloseSession(&pio_sess);
    TEEC_FinalizeContext(&pio_ctx);
	return 0;
}

enum protected_io_command {
	PIO_GENERATE_KEY_PAIR_FOR_SERVER       = (0x7000 << 2),
	PIO_GET_PUBLIC_KEY_FOR_SERVER          = (0x7001 << 2),
	PIO_CHANGE_PUBLIC_KEY_FOR_SERVER       = (0x7002 << 2),
	PIO_DELETE_KEY_PAIR_FOR_SERVER         = (0x7003 << 2),
	PIO_GET_ATTESTATION_FOR_SERVER         = (0x7004 << 2),
	PIO_GET_SIGNED_INPUT_FOR_SERVER        = (0x7005 << 2),
	PIO_GET_SECRET_INPUT_FOR_SERVER        = (0x7006 << 2),
	PIO_DISPLAY_MESSAGE_FROM_SERVER        = (0x7007 << 2),
	PIO_DISPLAY_SECRET_MESSAGE_FROM_SERVER = (0x7008 << 2),
	PIO_GET_USER_CONFIRM_FOR_SERVER        = (0x7009 << 2),
	PIO_DISPLAY_PUBLIC_KEY_FOR_SERVER      = (0x700A << 2),
	PIO_ABORT                              = (0x700B << 2)
};

int pio_test_gen_key(unsigned char *input_data, size_t input_data_len) {
	fprintf(stderr, "Key generation requested\n");
	return pio_test_run(input_data, input_data_len, PIO_GENERATE_KEY_PAIR_FOR_SERVER);
}

int pio_test_fetch_key(unsigned char *input_data, size_t input_data_len) {
	fprintf(stderr, "Key fetch requested\n");
	return pio_test_run(input_data, input_data_len, PIO_GET_PUBLIC_KEY_FOR_SERVER);
}

int pio_test_del_key(unsigned char *input_data, size_t input_data_len) {
	fprintf(stderr, "Key deletion requested\n");
	return pio_test_run(input_data, input_data_len, PIO_DELETE_KEY_PAIR_FOR_SERVER);
}

int pio_test_attest_key(unsigned char *input_data, size_t input_data_len) {
	fprintf(stderr, "Key attestation requested\n");
	return pio_test_run(input_data, input_data_len, PIO_GET_ATTESTATION_FOR_SERVER);
}

int pio_test_get_signed_input(unsigned char *input_data, size_t input_data_len) {
	fprintf(stderr, "Signed input requested\n");
	return pio_test_run(input_data, input_data_len, PIO_GET_SIGNED_INPUT_FOR_SERVER);
}

int pio_test_get_secret_input(unsigned char *input_data, size_t input_data_len) {
	fprintf(stderr, "Secret input requested\n");
	return pio_test_run(input_data, input_data_len, PIO_GET_SECRET_INPUT_FOR_SERVER);
}

int pio_test_show_signed_msg(unsigned char *input_data, size_t input_data_len) {
	fprintf(stderr, "Message display requested\n");
	return pio_test_run(input_data, input_data_len, PIO_DISPLAY_MESSAGE_FROM_SERVER);
}

int pio_test_show_secret_msg(unsigned char *input_data, size_t input_data_len) {
	fprintf(stderr, "Secret message display requested\n");
	return pio_test_run(input_data, input_data_len, PIO_DISPLAY_SECRET_MESSAGE_FROM_SERVER);
}

int pio_test_get_user_confirm(unsigned char *input_data, size_t input_data_len) {
	fprintf(stderr, "User confirmation requested\n");
	return pio_test_run(input_data, input_data_len, PIO_GET_USER_CONFIRM_FOR_SERVER);
}

static int8_t toInt(uint8_t c) {
    if(c >= 0x61U && c <= 0x66U) return (c - (0x61U - 0x0A));
    if(c >= 0x41U && c <= 0x46U) return (c - (0x41U - 0x0A));
    if(c >= 0x30U && c <= 0x39U) return (c - 0x30);
    return -1;
}

int pio_perf_runner_cmd_parser(int argc, char *argv[])
{
    if(argc != 3) {
		fprintf(stderr, "Atleast 2 arguments required, received only %u\n", argc);
		return 1;
	};

    uint32_t cmd = atoi(argv[1]);

	size_t input_len = strlen(argv[2]);

	fprintf(stderr, "Received hex string of length %lu\n", input_len);

	unsigned char *hex_str_input = (unsigned char *)argv[2];

	if(input_len < 2 || input_len % 2 != 0) {
		fprintf(stderr, "Received hex string has odd length\n");
		return 1;
	}

	unsigned char *cbor_input = malloc(input_len / 2);

	if(!cbor_input) {
		fprintf(stderr, "Unable to allocate memory for parsing hex string\n");
		return 1;
	}

    for(size_t index = 0; index < input_len - 1; index += 2) {
        int8_t c1 = toInt(hex_str_input[index]);
        int8_t c2 = toInt(hex_str_input[index + 1]);
        if(c1 == -1 || c2 == -1) return 1;
        cbor_input[index / 2U] = (c1 * 0x10 + c2);
    }

	fprintf(stderr, "Parsed the input hex string\n");

	int ret = 1;

	switch(cmd) {
	case 0:
		ret = pio_test_gen_key(cbor_input, input_len / 2);
		break;
	case 1:
		ret = pio_test_fetch_key(cbor_input, input_len / 2);
		break;
	case 2:
		ret = pio_test_del_key(cbor_input, input_len / 2);
		break;
	case 3:
		ret = pio_test_attest_key(cbor_input, input_len / 2);
		break;
	case 4:
		ret = pio_test_get_signed_input(cbor_input, input_len / 2);
		break;
	case 5:
		ret = pio_test_get_secret_input(cbor_input, input_len / 2);
		break;
	case 6:
		ret = pio_test_show_signed_msg(cbor_input, input_len / 2);
		break;
	case 7:
		ret = pio_test_show_secret_msg(cbor_input, input_len / 2);
		break;
	case 8:
		ret = pio_test_get_user_confirm(cbor_input, input_len / 2);
		break;
	default: {
		free(cbor_input);
		return 1;
	}
	}

	free(cbor_input);

	return ret;
}