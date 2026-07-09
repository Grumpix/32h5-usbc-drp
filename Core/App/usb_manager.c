#include "usb_manager.h"

#include "main.h"
#include "uart.h"
#include "tusb.h"

#include <stdbool.h>
#include <stdint.h>


/*
 * Local app hooks.
 */
void usb_device_tusb_init(void);
void usb_device_tusb_task(void);
void usb_device_tusb_deinit(void);

void usb_host_tusb_init(void);
void usb_host_tusb_task(void);
void usb_host_tusb_task_log(void);
void usb_host_tusb_deinit(void);


/*
 * TinyUSB core deinit.
 *
 * tuh_deinit() uz mas overene funkcni.
 * tud_deinit() pouzijeme pro ciste znovu-nastartovani CDC device stacku
 * po predchozim HOST rezimu.
 */
bool tuh_deinit(uint8_t rhport);
bool tud_deinit(uint8_t rhport);


static usb_mode_t usb_manager_mode =
    USB_MODE_NONE;


/*
 * Dirty flag:
 *
 * Jakmile probehne tud_init(), USB DRD peripheral je potencialne
 * nakonfigurovany device stackem.
 *
 * Pred startem hostu MUSIME udelat fyzicky USB DRD reset.
 */
static uint8_t usb_device_drd_dirty =
    0U;


/*
 * Jakmile probehne tuh_init(), host core/peripheral stav muze ovlivnit
 * dalsi device start. Pred dalsim DEVICE startem po HOST rezimu proto
 * udelame cisty HOST deinit + USB DRD reset.
 */
static uint8_t usb_host_drd_dirty =
    0U;


/*
 * Sledujeme, jestli jsme uz opravdu volali TinyUSB init.
 * Diky tomu nevolame deinit naslepo.
 */
static uint8_t usb_device_core_inited =
    0U;

static uint8_t usb_host_core_inited =
    0U;


static void usb_manager_force_usb_drd_reset(const char *reason)
{
    uart_write_str("[USB-DRD] FORCE RESET: ");
    uart_write_str(reason);
    uart_write_str("\r\n");

    HAL_NVIC_DisableIRQ(
        USB_DRD_FS_IRQn);

    HAL_NVIC_ClearPendingIRQ(
        USB_DRD_FS_IRQn);

    __DSB();
    __ISB();

    __HAL_RCC_USB_FORCE_RESET();

    for(volatile uint32_t i = 0U; i < 50000U; i++)
    {
        __NOP();
    }

    __HAL_RCC_USB_RELEASE_RESET();

    for(volatile uint32_t i = 0U; i < 50000U; i++)
    {
        __NOP();
    }

    /*
     * Nechame USB clock zapnuty.
     * TinyUSB init si periferii znovu nakonfiguruje.
     */
    __HAL_RCC_USB_CLK_ENABLE();

    HAL_NVIC_ClearPendingIRQ(
        USB_DRD_FS_IRQn);

    HAL_NVIC_EnableIRQ(
        USB_DRD_FS_IRQn);

    uart_write_str("[USB-DRD] FORCE RESET DONE\r\n");
}


static void usb_manager_device_core_deinit_if_needed(void)
{
    if(usb_device_core_inited == 0U)
    {
        uart_write_str("[USB-DEVICE] tud_deinit skipped - not inited\r\n");
        return;
    }

    uart_write_str("[USB-DEVICE] BEFORE tud_deinit\r\n");

    if(tud_deinit(0))
    {
        uart_write_str("[USB-DEVICE] AFTER tud_deinit OK\r\n");
    }
    else
    {
        uart_write_str("[USB-DEVICE] AFTER tud_deinit FAIL\r\n");
    }

    usb_device_core_inited =
        0U;
}


static void usb_manager_host_core_deinit_if_needed(void)
{
    if(usb_host_core_inited == 0U)
    {
        uart_write_str("[USB-HOST] tuh_deinit skipped - not inited\r\n");
        return;
    }

    uart_write_str("[USB-HOST] BEFORE tuh_deinit\r\n");

    if(tuh_deinit(0))
    {
        uart_write_str("[USB-HOST] AFTER tuh_deinit OK\r\n");
    }
    else
    {
        uart_write_str("[USB-HOST] AFTER tuh_deinit FAIL\r\n");
    }

    usb_host_core_inited =
        0U;
}


static void usb_manager_clear_device_dirty_before_host(const char *reason)
{
    if(usb_device_drd_dirty == 0U)
    {
        uart_write_str("[USB-DRD] device dirty=0, reset not needed\r\n");
        return;
    }

    uart_write_str("[USB-DRD] device dirty=1 -> reset before HOST\r\n");

    usb_device_tusb_deinit();

    usb_manager_device_core_deinit_if_needed();

    usb_manager_force_usb_drd_reset(
        reason);

    usb_device_drd_dirty =
        0U;
}


static void usb_manager_clear_host_dirty_before_device(const char *reason)
{
    if(usb_host_drd_dirty == 0U)
    {
        uart_write_str("[USB-DRD] host dirty=0, reset not needed\r\n");
        return;
    }

    uart_write_str("[USB-DRD] host dirty=1 -> reset before DEVICE\r\n");

    usb_host_tusb_deinit();

    usb_manager_host_core_deinit_if_needed();

    usb_manager_force_usb_drd_reset(
        reason);

    usb_host_drd_dirty =
        0U;
}


void usb_manager_init(void)
{
    usb_manager_mode =
        USB_MODE_NONE;

    usb_device_drd_dirty =
        0U;

    usb_host_drd_dirty =
        0U;

    usb_device_core_inited =
        0U;

    usb_host_core_inited =
        0U;

    uart_write_str("[USB] manager init\r\n");
}


bool usb_manager_start_device(void)
{
    uart_write_str("[USB-DEVICE] START\r\n");

    if(usb_manager_mode == USB_MODE_DEVICE)
    {
        uart_write_str("[USB-DEVICE] already active\r\n");
        return true;
    }

    if(usb_manager_mode == USB_MODE_HOST)
    {
        uart_write_str("[USB-DEVICE] stopping host first\r\n");

        usb_host_tusb_deinit();

        usb_manager_host_core_deinit_if_needed();

        usb_manager_force_usb_drd_reset(
            "HOST -> DEVICE");

        usb_host_drd_dirty =
            0U;

        usb_manager_mode =
            USB_MODE_NONE;
    }
    else
    {
        /*
         * DULEZITE PRO HOST -> DEVICE:
         *
         * Pokud uz host predtim bezel a byl zastaven, usb_manager_mode muze
         * byt NONE, ale USB/TinyUSB stav porad mohl ovlivnit dalsi CDC start.
         */
        usb_manager_clear_host_dirty_before_device(
            "dirty HOST -> DEVICE");
    }

    uart_write_str("[USB-DEVICE] BEFORE usb_device_tusb_init\r\n");

    usb_device_tusb_init();

    uart_write_str("[USB-DEVICE] AFTER usb_device_tusb_init\r\n");

    /*
     * Mode nastavime pred tud_init(), aby USB interrupt behem initu
     * sel spravne do device handleru.
     */
    usb_manager_mode =
        USB_MODE_DEVICE;

    uart_write_str("[USB-DEVICE] BEFORE tud_init\r\n");

    tud_init(0);

    usb_device_core_inited =
        1U;

    /*
     * Od teto chvile je USB DRD periferie "dirty" od device stacku.
     * Pred hostem bude potreba fyzicky reset.
     */
    usb_device_drd_dirty =
        1U;

    uart_write_str("[USB-DEVICE] AFTER tud_init\r\n");

    uart_write_str("[USB] DEVICE mode\r\n");
    uart_write_str("[USB-DEVICE] START DONE\r\n");

    return true;
}


bool usb_manager_start_host(void)
{
    uart_write_str("[USB-HOST] START\r\n");

    if(usb_manager_mode == USB_MODE_HOST)
    {
        uart_write_str("[USB-HOST] already active\r\n");
        return true;
    }

    if(usb_manager_mode == USB_MODE_DEVICE)
    {
        uart_write_str("[USB-HOST] leaving DEVICE -> reset USB DRD first\r\n");

        usb_manager_clear_device_dirty_before_host(
            "DEVICE -> HOST");
    }
    else
    {
        /*
         * I kdyz usb_manager_mode uz neni DEVICE, device stack mohl predtim
         * bezet a USB DRD peripheral zustal ve spatnem stavu.
         */
        usb_manager_clear_device_dirty_before_host(
            "dirty NONE -> HOST");
    }

    usb_manager_mode =
        USB_MODE_NONE;

    uart_write_str("[USB-HOST] BEFORE usb_host_tusb_init\r\n");

    usb_host_tusb_init();

    uart_write_str("[USB-HOST] AFTER usb_host_tusb_init\r\n");

    /*
     * Mode nastavime pred tuh_init(), aby USB_DRD_FS_IRQHandler()
     * uz behem host initu smeroval interrupt do tuh_int_handler(0).
     */
    usb_manager_mode =
        USB_MODE_HOST;

    uart_write_str("[USB-HOST] BEFORE tuh_init\r\n");

    tuh_init(0);

    usb_host_core_inited =
        1U;

    /*
     * Host core/peripheral je od teto chvile dirty pro budouci DEVICE start.
     */
    usb_host_drd_dirty =
        1U;

    uart_write_str("[USB-HOST] AFTER tuh_init\r\n");

    uart_write_str("[USB] HOST mode\r\n");
    uart_write_str("[USB-HOST] START DONE\r\n");

    return true;
}


void usb_manager_stop(void)
{
    uart_write_str("[USB] STOP\r\n");

    if(usb_manager_mode == USB_MODE_HOST)
    {
        uart_write_str("[USB] stopping host\r\n");

        /*
         * App-level host state reset.
         */
        usb_host_tusb_deinit();

        /*
         * TinyUSB host core deinit.
         *
         * Bez toho druhy tuh_init(0) nevola znovu app class init.
         */
        usb_manager_host_core_deinit_if_needed();

        /*
         * DULEZITE PRO MSC -> FTDI HOTPLUG:
         *
         * Po odpojeni USB zarizeni zastavime host stack a rovnou fyzicky
         * resetujeme USB DRD peripheral.
         *
         * Volano z Type-C detach cesty az po VBUS FET OFF,
         * ne primo z TinyUSB callbacku.
         */
        usb_manager_force_usb_drd_reset(
            "HOST stop");

        /*
         * Host je po tuh_deinit + DRD resetu cisty pro dalsi HOST start.
         * Zaroven ale pred DEVICE startem uz taky neni potreba dalsi reset.
         */
        usb_host_drd_dirty =
            0U;
    }
    else if(usb_manager_mode == USB_MODE_DEVICE)
    {
        uart_write_str("[USB] stopping device -> force USB DRD reset\r\n");

        usb_device_tusb_deinit();

        /*
         * DULEZITE PRO HOST -> DEVICE -> HOST opakovani:
         *
         * Pred opustenim DEVICE rezimu deinitujeme i TinyUSB device core.
         */
        usb_manager_device_core_deinit_if_needed();

        usb_manager_force_usb_drd_reset(
            "DEVICE stop");

        usb_device_drd_dirty =
            0U;
    }
    else
    {
        uart_write_str("[USB] already stopped\r\n");
    }

    usb_manager_mode =
        USB_MODE_NONE;

    uart_write_str("[USB] STOP DONE\r\n");
}


void usb_manager_task(void)
{
    if(usb_manager_mode == USB_MODE_HOST)
    {
        usb_host_tusb_task();
        usb_host_tusb_task_log();
    }
    else if(usb_manager_mode == USB_MODE_DEVICE)
    {
        usb_device_tusb_task();
    }
}


bool usb_manager_toggle_mode(void)
{
    bool ok;

    if(usb_manager_mode == USB_MODE_HOST)
    {
        uart_write_str("[USB] toggle HOST -> DEVICE\r\n");

        usb_manager_stop();

        ok =
            usb_manager_start_device();
    }
    else
    {
        uart_write_str("[USB] toggle -> HOST\r\n");

        usb_manager_stop();

        ok =
            usb_manager_start_host();
    }

    return ok;
}


bool usb_manager_is_host_active(void)
{
    return
        (usb_manager_mode == USB_MODE_HOST);
}


bool usb_manager_is_device_active(void)
{
    return
        (usb_manager_mode == USB_MODE_DEVICE);
}


bool usb_manager_is_active(void)
{
    return
        (usb_manager_mode != USB_MODE_NONE);
}


usb_mode_t usb_manager_get_mode(void)
{
    return
        usb_manager_mode;
}