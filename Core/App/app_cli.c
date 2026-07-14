#include "app_cli.h"

#include "app_usb_status.h"
#include "m16c_vnc_bus.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>


#define APP_CLI_FW_NAME       "stm32h533-usbc-drp"
#define APP_CLI_VERSION       "0.9-m16c-trace-command"


static void out_append(
    char *out,
    uint32_t out_size,
    uint32_t *pos,
    const char *text)
{
    if((out == NULL) || (pos == NULL) || (text == NULL) || (out_size == 0U))
    {
        return;
    }

    while((*text != '\0') && ((*pos + 1U) < out_size))
    {
        out[*pos] =
            *text;

        (*pos)++;
        text++;
    }

    out[*pos] =
        '\0';
}


static void out_append_bool(
    char *out,
    uint32_t out_size,
    uint32_t *pos,
    bool value)
{
    out_append(
        out,
        out_size,
        pos,
        value ? "1" : "0");
}


static void out_append_port(
    char *out,
    uint32_t out_size,
    uint32_t *pos,
    app_cli_port_t port)
{
    if(port == APP_CLI_PORT_FTDI)
    {
        out_append(out, out_size, pos, "FTDI");
    }
    else
    {
        out_append(out, out_size, pos, "CDC");
    }
}


static bool str_eq(const char *a, const char *b)
{
    if((a == NULL) || (b == NULL))
    {
        return false;
    }

    while((*a != '\0') && (*b != '\0'))
    {
        if(*a != *b)
        {
            return false;
        }

        a++;
        b++;
    }

    return
        (*a == '\0') && (*b == '\0');
}


static void copy_trimmed_lower(
    const char *line,
    char *cmd,
    uint32_t cmd_size)
{
    uint32_t in_pos = 0U;
    uint32_t out_pos = 0U;

    if((cmd == NULL) || (cmd_size == 0U))
    {
        return;
    }

    cmd[0] =
        '\0';

    if(line == NULL)
    {
        return;
    }

    while(
        (line[in_pos] == ' ') ||
        (line[in_pos] == '\t') ||
        (line[in_pos] == '\r') ||
        (line[in_pos] == '\n')
    )
    {
        in_pos++;
    }

    while((line[in_pos] != '\0') && ((out_pos + 1U) < cmd_size))
    {
        char c =
            line[in_pos];

        if((c == '\r') || (c == '\n'))
        {
            break;
        }

        if((c >= 'A') && (c <= 'Z'))
        {
            c =
                (char)(c - 'A' + 'a');
        }

        cmd[out_pos] =
            c;

        out_pos++;
        in_pos++;
    }

    while(
        (out_pos > 0U) &&
        (
            (cmd[out_pos - 1U] == ' ') ||
            (cmd[out_pos - 1U] == '\t')
        )
    )
    {
        out_pos--;
    }

    cmd[out_pos] =
        '\0';
}


static void cli_cmd_help(
    app_cli_port_t port,
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    out_append(out, out_size, &pos, "USB CLI on ");
    out_append_port(out, out_size, &pos, port);
    out_append(out, out_size, &pos, "\r\n");

    out_append(out, out_size, &pos, "Commands:\r\n");
    out_append(out, out_size, &pos, "  help, ?       - show commands\r\n");
    out_append(out, out_size, &pos, "  ping          - connectivity test\r\n");
    out_append(out, out_size, &pos, "  version       - firmware/build info\r\n");
    out_append(out, out_size, &pos, "  role          - current USB-C role\r\n");
    out_append(out, out_size, &pos, "  status        - full USB status\r\n");
    out_append(out, out_size, &pos, "  usb           - alias for status\r\n");

    out_append(out, out_size, &pos, "  m16c          - M16C/VNC bus monitor\r\n");
    out_append(out, out_size, &pos, "  m16c trace    - show TX/RD trace only\r\n");
    out_append(out, out_size, &pos, "  m16c reset    - reset M16C/VNC counters\r\n");
    out_append(out, out_size, &pos, "  m16c idle     - TXE#=1, stop WR capture\r\n");
    out_append(out, out_size, &pos, "  m16c ready    - TXE#=0 only, no WR IRQ arm\r\n");
    out_append(out, out_size, &pos, "  m16c armwr    - arm temporary WR capture, TXE#=0\r\n");
    out_append(out, out_size, &pos, "  m16c stopwr   - stop WR capture, TXE#=1\r\n");
    out_append(out, out_size, &pos, "  m16c clear    - clear STM32->M16C FIFO\r\n");
    out_append(out, out_size, &pos, "  m16c boot     - queue simple boot prompt\r\n");

    out_append(out, out_size, &pos, "  m16c pc       - queue SDA CR event\r\n");
    out_append(out, out_size, &pos, "  m16c pc2      - queue SDA CR prompt event\r\n");
    out_append(out, out_size, &pos, "  m16c pc3      - queue SDA CR drive prompt event\r\n");
    out_append(out, out_size, &pos, "  m16c pc4      - queue SDA CR CR prompt event\r\n");

    out_append(out, out_size, &pos, "  m16c nopc     - queue SDD CR event\r\n");
    out_append(out, out_size, &pos, "  m16c dd2      - queue DD2 CR event\r\n");
}


static void cli_cmd_ping(
    app_cli_port_t port,
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    out_append(out, out_size, &pos, "pong from ");
    out_append_port(out, out_size, &pos, port);
    out_append(out, out_size, &pos, "\r\n");
}


static void cli_cmd_version(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    out_append(out, out_size, &pos, "fw=");
    out_append(out, out_size, &pos, APP_CLI_FW_NAME);

    out_append(out, out_size, &pos, " version=");
    out_append(out, out_size, &pos, APP_CLI_VERSION);

    out_append(out, out_size, &pos, " build=");
    out_append(out, out_size, &pos, __DATE__);

    out_append(out, out_size, &pos, " ");
    out_append(out, out_size, &pos, __TIME__);

    out_append(out, out_size, &pos, "\r\n");
}


static void cli_cmd_role(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    app_usb_role_t role =
        app_usb_get_role();

    out_append(out, out_size, &pos, "role=");
    out_append(out, out_size, &pos, app_usb_role_name(role));
    out_append(out, out_size, &pos, "\r\n");
}


static void cli_cmd_status(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    app_usb_status_t s;

    app_usb_status_get(&s);

    out_append(out, out_size, &pos, "status role=");
    out_append(out, out_size, &pos, app_usb_role_name(s.role));

    out_append(out, out_size, &pos, " attached=");
    out_append_bool(out, out_size, &pos, s.typec_attached);

    out_append(out, out_size, &pos, " unattached=");
    out_append_bool(out, out_size, &pos, s.typec_unattached);

    out_append(out, out_size, &pos, " vbus=");
    out_append_bool(out, out_size, &pos, s.vbus_present);

    out_append(out, out_size, &pos, " usb=");
    out_append_bool(out, out_size, &pos, s.usb_started);

    out_append(out, out_size, &pos, " pc=");
    out_append_bool(out, out_size, &pos, s.pc_connected);

    out_append(out, out_size, &pos, " cdc=");
    out_append_bool(out, out_size, &pos, s.pc_cdc_ready);

    out_append(out, out_size, &pos, " host=");
    out_append_bool(out, out_size, &pos, s.host_active);

    out_append(out, out_size, &pos, " hdev=");
    out_append_bool(out, out_size, &pos, s.host_device_attached);

    out_append(out, out_size, &pos, " msc=");
    out_append_bool(out, out_size, &pos, s.msc_mounted);

    out_append(out, out_size, &pos, " ftdi=");
    out_append_bool(out, out_size, &pos, s.ftdi_mounted);

    out_append(out, out_size, &pos, " ftdi_ready=");
    out_append_bool(out, out_size, &pos, s.ftdi_ready);

    out_append(out, out_size, &pos, "\r\n");
}


static void cli_cmd_m16c(
    char *out,
    uint32_t out_size)
{
    m16c_vnc_bus_format_status(
        out,
        out_size);
}


static void cli_cmd_m16c_trace(
    char *out,
    uint32_t out_size)
{
    m16c_vnc_bus_format_trace(
        out,
        out_size);
}


static void cli_cmd_m16c_reset(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_reset_counters();

    out_append(out, out_size, &pos, "m16c counters reset\r\n");
}


static void cli_cmd_m16c_idle(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_stop_wr_capture();
    m16c_vnc_bus_set_ready(0U);

    out_append(out, out_size, &pos, "m16c idle: TXE#=1, WR capture stopped\r\n");
}


static void cli_cmd_m16c_ready(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    /*
     * Bezpecne ready:
     * jen TXE#=0, ale nearmuje WR EXTI.
     * WR capture se zapina samostatne pres m16c armwr.
     */
    m16c_vnc_bus_set_ready(1U);

    out_append(out, out_size, &pos, "m16c ready: TXE#=0, WR capture not armed\r\n");
}


static void cli_cmd_m16c_armwr(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_arm_wr_capture();

    out_append(out, out_size, &pos, "m16c WR capture armed temporarily\r\n");
}


static void cli_cmd_m16c_stopwr(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_stop_wr_capture();

    out_append(out, out_size, &pos, "m16c WR capture stopped\r\n");
}


static void cli_cmd_m16c_clear(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_clear_tx_fifo();

    out_append(out, out_size, &pos, "m16c fifo cleared\r\n");
}


static void cli_cmd_m16c_boot(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_queue_boot_banner();

    out_append(out, out_size, &pos, "m16c queued boot banner\r\n");
}


static void cli_cmd_m16c_pc(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_queue_pc_attached();

    out_append(out, out_size, &pos, "m16c queued SDA / PC Control target\r\n");
}


static void cli_cmd_m16c_pc2(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_queue_pc_attached_prompt();

    out_append(out, out_size, &pos, "m16c queued SDA CR prompt / PC Control target\r\n");
}


static void cli_cmd_m16c_pc3(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_queue_pc_attached_drive_prompt();

    out_append(out, out_size, &pos, "m16c queued SDA CR D prompt / PC Control target\r\n");
}


static void cli_cmd_m16c_pc4(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_queue_pc_attached_double_prompt();

    out_append(out, out_size, &pos, "m16c queued SDA CR CR prompt / PC Control target\r\n");
}


static void cli_cmd_m16c_nopc(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_queue_pc_detached();

    out_append(out, out_size, &pos, "m16c queued SDD\r\n");
}


static void cli_cmd_m16c_dd2(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_queue_host_device_attached();

    out_append(out, out_size, &pos, "m16c queued DD2\r\n");
}


void app_cli_execute_line(
    app_cli_port_t port,
    const char *line,
    char *out,
    uint32_t out_size)
{
    char cmd[64];

    if((out == NULL) || (out_size == 0U))
    {
        return;
    }

    out[0] =
        '\0';

    copy_trimmed_lower(
        line,
        cmd,
        sizeof(cmd));

    if((cmd[0] == '\0') || str_eq(cmd, "help") || str_eq(cmd, "?"))
    {
        cli_cmd_help(
            port,
            out,
            out_size);
    }
    else if(str_eq(cmd, "ping"))
    {
        cli_cmd_ping(
            port,
            out,
            out_size);
    }
    else if(str_eq(cmd, "version"))
    {
        cli_cmd_version(
            out,
            out_size);
    }
    else if(str_eq(cmd, "role"))
    {
        cli_cmd_role(
            out,
            out_size);
    }
    else if(str_eq(cmd, "status") || str_eq(cmd, "usb"))
    {
        cli_cmd_status(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c"))
    {
        cli_cmd_m16c(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c trace"))
    {
        cli_cmd_m16c_trace(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c reset"))
    {
        cli_cmd_m16c_reset(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c idle"))
    {
        cli_cmd_m16c_idle(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c ready"))
    {
        cli_cmd_m16c_ready(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c armwr"))
    {
        cli_cmd_m16c_armwr(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c stopwr"))
    {
        cli_cmd_m16c_stopwr(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c clear"))
    {
        cli_cmd_m16c_clear(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c boot"))
    {
        cli_cmd_m16c_boot(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c pc"))
    {
        cli_cmd_m16c_pc(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c pc2"))
    {
        cli_cmd_m16c_pc2(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c pc3"))
    {
        cli_cmd_m16c_pc3(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c pc4"))
    {
        cli_cmd_m16c_pc4(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c nopc"))
    {
        cli_cmd_m16c_nopc(
            out,
            out_size);
    }
    else if(str_eq(cmd, "m16c dd2"))
    {
        cli_cmd_m16c_dd2(
            out,
            out_size);
    }
    else
    {
        uint32_t pos =
            0U;

        out_append(out, out_size, &pos, "ERR unknown command: ");
        out_append(out, out_size, &pos, cmd);
        out_append(out, out_size, &pos, "\r\n");
    }
}