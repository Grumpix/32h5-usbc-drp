#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t bus;

    uint8_t rd;
    uint8_t wr;
    uint8_t datareq;
    uint8_t ioreset;
    uint8_t txe;
    uint8_t rxf;

    uint32_t rd_falling_count;
    uint32_t rd_rising_count;
    uint32_t wr_falling_count;
    uint32_t wr_rising_count;
    uint32_t datareq_falling_count;
    uint32_t datareq_rising_count;
    uint32_t ioreset_falling_count;
    uint32_t ioreset_rising_count;

    uint8_t last_wr_bus;
    uint8_t last_rd_bus;
} m16c_vnc_bus_status_t;


void m16c_vnc_bus_init(void);
void m16c_vnc_bus_task(void);

void m16c_vnc_bus_rd_irq(void);
void m16c_vnc_bus_wr_irq(void);

void m16c_vnc_bus_get_status(
    m16c_vnc_bus_status_t *status);

void m16c_vnc_bus_reset_counters(void);

void m16c_vnc_bus_set_ready(
    uint8_t ready);

uint8_t m16c_vnc_bus_is_ready(void);

void m16c_vnc_bus_clear_tx_fifo(void);

void m16c_vnc_bus_queue_bytes(
    const uint8_t *data,
    uint32_t len);

void m16c_vnc_bus_queue_string(
    const char *text);

void m16c_vnc_bus_queue_boot_banner(void);
void m16c_vnc_bus_queue_pc_attached(void);
void m16c_vnc_bus_queue_pc_detached(void);
void m16c_vnc_bus_queue_host_device_attached(void);

void m16c_vnc_bus_format_status(
    char *out,
    uint32_t out_size);

#ifdef __cplusplus
}
#endif