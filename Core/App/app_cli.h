#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CLI_OUTPUT_MAX 384U

typedef enum
{
    APP_CLI_PORT_CDC = 0,
    APP_CLI_PORT_FTDI
} app_cli_port_t;


void app_cli_execute_line(
    app_cli_port_t port,
    const char *line,
    char *out,
    uint32_t out_size);

#ifdef __cplusplus
}
#endif