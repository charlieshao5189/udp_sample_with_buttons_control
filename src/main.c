/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <stdio.h>
#include <modem/lte_lc.h>
#include <zephyr/net/socket.h>
#include <nrf_modem_at.h>
#include <device.h>
#include <drivers/gpio.h>

#define UDP_IP_HEADER_SIZE 28

/*
 * Get button configuration from the devicetree sw0 alias. This is mandatory.
 */
#define BT1_NODE DT_ALIAS(sw0)
#define BT2_NODE DT_ALIAS(sw1)
#define SW1_NODE DT_ALIAS(sw2)
#define SW2_NODE DT_ALIAS(sw3)

#if !(DT_NODE_HAS_STATUS(BT1_NODE, okay) && DT_NODE_HAS_STATUS(BT2_NODE, okay) && DT_NODE_HAS_STATUS(SW1_NODE, okay) && DT_NODE_HAS_STATUS(SW2_NODE, okay))
#error "Unsupported board: button devicetree alias is not defined"
#endif
static const struct gpio_dt_spec buttons[] = {
	GPIO_DT_SPEC_GET_OR(BT1_NODE, gpios,
						{0}),
	GPIO_DT_SPEC_GET_OR(BT2_NODE, gpios,
						{0}),
	GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios,
						{0}),
	GPIO_DT_SPEC_GET_OR(SW2_NODE, gpios,
						{0})};
static struct gpio_callback button_cb_data[4];

static int client_fd;
static struct sockaddr_storage host_addr;
static struct k_work_delayable server_transmission_work;
static struct k_work_delayable lte_set_connection_work;

static volatile enum state_type { LTE_STATE_ON,
                                  LTE_STATE_OFFLINE,
                                  LTE_STATE_BUSY } LTE_Connection_Current_State;
static volatile enum state_type LTE_Connection_Target_State;
static volatile bool PSM_Enable;
static volatile bool RAI_Enable;
static volatile bool PSM_Enable_pr;
static volatile bool RAI_Enable_pr;

int lte_lc_rel14feat_rai_req(bool enable)
{ /** Need to enable release 14 RAI feature at offline mode before run this command **/
        // err = nrf_modem_at_printf("AT%%REL14FEAT=0,1,0,0,0");
        // if (err) {
        // 	printk("Release 14 RAI feature AT-command failed, err %d", err);
        // }
        int err;
        enum lte_lc_system_mode mode;

        err = lte_lc_system_mode_get(&mode, NULL);
        if (err)
        {
                return -EFAULT;
        }

        switch (mode)
        {
        case LTE_LC_SYSTEM_MODE_LTEM:
        case LTE_LC_SYSTEM_MODE_LTEM_GPS:
        case LTE_LC_SYSTEM_MODE_NBIOT:
        case LTE_LC_SYSTEM_MODE_NBIOT_GPS:
        case LTE_LC_SYSTEM_MODE_LTEM_NBIOT:
        case LTE_LC_SYSTEM_MODE_LTEM_NBIOT_GPS:
                break;
        default:
                printk("Unknown system mode");
                printk("Cannot request RAI for unknown system mode");
                return -EOPNOTSUPP;
        }

        if (enable)
        {
                err = nrf_modem_at_printf("AT%%RAI=1");
        }
        else
        {
                err = nrf_modem_at_printf("AT%%RAI=0");
        }

        if (err)
        {
                printk("nrf_modem_at_printf failed, reported error: %d", err);
                return -EFAULT;
        }

        return 0;
}

void button_pressed(const struct device *dev, struct gpio_callback *cb,
                    uint32_t pins)
{
        int val;

        printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());

        val = gpio_pin_get_dt(&buttons[0]);
        if (val == 1 && LTE_Connection_Current_State == LTE_STATE_ON) // button1 pressed
        {
                printk("Send UDP package!\n");
                k_work_reschedule(&server_transmission_work, K_NO_WAIT);
        }

        val = gpio_pin_get_dt(&buttons[1]);
        if (val == 1) // button2 pressed
        {
                if (LTE_Connection_Current_State == LTE_STATE_ON)
                {
                        LTE_Connection_Target_State = LTE_STATE_OFFLINE;
                        k_work_reschedule(&lte_set_connection_work, K_NO_WAIT);
                }
                else if (LTE_Connection_Current_State == LTE_STATE_OFFLINE)
                {
                        LTE_Connection_Target_State = LTE_STATE_ON;
                        k_work_reschedule(&lte_set_connection_work, K_NO_WAIT);
                }
        }

#if defined(CONFIG_UDP_PSM_ENABLE)
        val = gpio_pin_get_dt(&buttons[2]);
        PSM_Enable = val;
        if (PSM_Enable != PSM_Enable_pr)
        {
                if (LTE_STATE_ON == LTE_Connection_Current_State)
                {
                        printk("PSM mode setting is changed, configure and reconnect network!\n");
                        k_work_reschedule(&lte_set_connection_work, K_NO_WAIT);
                        LTE_Connection_Target_State = LTE_STATE_ON;
                }
                PSM_Enable_pr = PSM_Enable;
        }
#else
        printk("PSM is not enabled in prj.conf!");
#endif

#if defined(CONFIG_UDP_RAI_ENABLE)
        val = gpio_pin_get_dt(&buttons[3]);
        RAI_Enable = val;
        if (RAI_Enable != RAI_Enable_pr)
        {
                printk("RAI is %s!",(RAI_Enable)? "ENABLED":"DISABLED");
                if (LTE_STATE_ON == LTE_Connection_Current_State)
                {
                        printk("RAI feature setting is changed, configure and reconnect network!\n");
                        k_work_reschedule(&lte_set_connection_work, K_NO_WAIT);
                        LTE_Connection_Target_State = LTE_STATE_ON;
                }
                RAI_Enable_pr = RAI_Enable;
        }
#else
        printk("RAI is not enabled in prj.conf!");
#endif

}

void button_init(void)
{
        int ret;
        for (size_t i = 0; i < ARRAY_SIZE(buttons); i++)
        {
                ret = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
                if (ret != 0)
                {
                        printk("Error %d: failed to configure %s pin %d\n",
                               ret, buttons[i].port->name, buttons[i].pin);
                        return;
                }

                ret = gpio_pin_interrupt_configure_dt(&buttons[i],
                                                      GPIO_INT_EDGE_TO_ACTIVE);
                if (ret != 0)
                {
                        printk("Error %d: failed to configure interrupt on %s pin %d\n",
                               ret, buttons[i].port->name, buttons[i].pin);
                        return;
                }

                gpio_init_callback(&button_cb_data[i], button_pressed, BIT(buttons[i].pin));
                gpio_add_callback(buttons[i].port, &button_cb_data[i]);
                printk("Set up button at %s pin %d\n", buttons[i].port->name, buttons[i].pin);
        }
}

static void server_disconnect(void)
{
        (void)close(client_fd);
}

static int server_init(void)
{
        struct sockaddr_in *server4 = ((struct sockaddr_in *)&host_addr);

        server4->sin_family = AF_INET;
        server4->sin_port = htons(CONFIG_UDP_SERVER_PORT);

        inet_pton(AF_INET, CONFIG_UDP_SERVER_ADDRESS_STATIC,
                  &server4->sin_addr);

        return 0;
}

static int server_connect(void)
{
        int err;

        client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (client_fd < 0)
        {
                printk("Failed to create UDP socket: %d\n", errno);
                err = -errno;
                goto error;
        }

        err = connect(client_fd, (struct sockaddr *)&host_addr,
                      sizeof(struct sockaddr_in));
        if (err < 0)
        {
                printk("Connect failed : %d\n", err);
                goto error;
        }

        return 0;

error:
        server_disconnect();

        return err;
}

#if defined(CONFIG_NRF_MODEM_LIB)
static void lte_handler(const struct lte_lc_evt *const evt)
{
        switch (evt->type)
        {
        case LTE_LC_EVT_NW_REG_STATUS:
                printk("Network registration status: %d\n",
                       evt->nw_reg_status);

                if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
                    (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING))
                {
                        if (evt->nw_reg_status == 0)
                        {
                                LTE_Connection_Current_State = LTE_STATE_OFFLINE;
                                printk("LTE OFFLINE!\n");
                                break;
                        }
                        break;
                }

                printk("Network registration status: %s\n",
                       evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ? "Connected - home network" : "Connected - roaming\n");
                LTE_Connection_Current_State = LTE_STATE_ON;
                break;
        case LTE_LC_EVT_PSM_UPDATE:
                printk("PSM parameter update: TAU: %d, Active time: %d\n",
                       evt->psm_cfg.tau, evt->psm_cfg.active_time);
                break;
        case LTE_LC_EVT_EDRX_UPDATE:
        {
                char log_buf[60];
                ssize_t len;

                len = snprintf(log_buf, sizeof(log_buf),
                               "eDRX parameter update: eDRX: %f, PTW: %f\n",
                               evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
                if (len > 0)
                {
                        printk("%s\n", log_buf);
                }
                break;
        }
        case LTE_LC_EVT_RRC_UPDATE:
                printk("RRC mode: %s\n",
                       evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle\n");
                break;
        case LTE_LC_EVT_CELL_UPDATE:
                printk("LTE cell changed: Cell ID: %d, Tracking area: %d\n",
                       evt->cell.id, evt->cell.tac);
                break;
        default:
                break;
        }
}

static int configure_low_power(void)
{
        int err;

#if defined(CONFIG_UDP_PSM_ENABLE)
        /** Power Saving Mode */
        err = lte_lc_psm_req(PSM_Enable);
        if (err)
        {
                printk("lte_lc_psm_req, error: %d\n", err);
        }
#else
        err = lte_lc_psm_req(false);
        if (err)
        {
                printk("lte_lc_psm_req, error: %d\n", err);
        }
#endif

#if defined(CONFIG_UDP_EDRX_ENABLE)
        /** enhanced Discontinuous Reception */
        err = lte_lc_edrx_req(true);
        if (err)
        {
                printk("lte_lc_edrx_req, error: %d\n", err);
        }
#else
        err = lte_lc_edrx_req(false);
        if (err)
        {
                printk("lte_lc_edrx_req, error: %d\n", err);
        }
#endif

#if defined(CONFIG_UDP_RAI_ENABLE)
        /** Enable release 14 RAI feature **/
        err = nrf_modem_at_printf("AT%%REL14FEAT=0,1,0,0,0");
        if (err)
        {
                printk("Release 14 RAI feature AT-command failed, err %d", err);
        }
        /** Release Assistance Indication  */
        err = lte_lc_rel14feat_rai_req(RAI_Enable);
        if (err)
        {
                printk("lte_lc_rel14feat_rai_req, error: %d\n", err);
        }
#endif

        return err;
}

static void modem_init(void)
{
        int err;

        if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT))
        {
                /* Do nothing, modem is already configured and LTE connected. */
        }
        else
        {
                err = lte_lc_init();
                if (err)
                {
                        printk("Modem initialization failed, error: %d\n", err);
                        return;
                }
        }
}

static void modem_connect(void)
{
        int err;

        if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT))
        {
                /* Do nothing, modem is already configured and LTE connected. */
        }
        else
        {
                err = lte_lc_connect_async(lte_handler);
                if (err)
                {
                        printk("Connecting to LTE network failed, error: %d\n",
                               err);
                        return;
                }
        }
}
#endif

static void server_transmission_work_fn(struct k_work *work)
{
        int err;
        char buffer[CONFIG_UDP_DATA_UPLOAD_SIZE_BYTES] = {"\0"};

        server_connect();
        printk("Transmitting UDP/IP payload of %d bytes to the ",
               CONFIG_UDP_DATA_UPLOAD_SIZE_BYTES + UDP_IP_HEADER_SIZE);
        printk("IP address %s, port number %d\n",
               CONFIG_UDP_SERVER_ADDRESS_STATIC,
               CONFIG_UDP_SERVER_PORT);

        if (RAI_Enable)
        {
                printk("Setting socket option to RAI_LAST to send last package!\n");
                err = setsockopt(client_fd, SOL_SOCKET, SO_RAI_LAST, NULL, 0);
                if (err < 0){
                printk("Set socket option RAI_LAST failed : %d\n", err);
                }
        }

        err = send(client_fd, buffer, sizeof(buffer), 0);
        if (err < 0)
        {
                printk("Failed to transmit UDP packet, %d\n", err);
                return;
        }
        server_disconnect();
        k_work_schedule(&server_transmission_work,
                        K_SECONDS(CONFIG_UDP_DATA_UPLOAD_FREQUENCY_SECONDS));
}

static void lte_set_connection_work_fn(struct k_work *work)
{
        int err;
        if (LTE_STATE_BUSY == LTE_Connection_Target_State)
        {
                LTE_Connection_Current_State = LTE_STATE_BUSY;
        }
        else
        {
                LTE_Connection_Current_State = LTE_STATE_BUSY;
                if (LTE_STATE_OFFLINE == LTE_Connection_Target_State)
                {
                        lte_lc_offline();
                }
                else if (LTE_STATE_ON == LTE_Connection_Target_State)
                {
                        lte_lc_offline();
#if defined(CONFIG_UDP_PSM_ENABLE)
                        err = lte_lc_psm_req(PSM_Enable);
                        if (err)
                        {
                                printk("lte_lc_psm_req, error: %d\n", err);
                        }
#endif
#if defined(CONFIG_UDP_RAI_ENABLE)
                        err = lte_lc_rel14feat_rai_req(RAI_Enable);
                        if (err)
                        {
                                printk("lte_lc_rel14feat_rai_req, error: %d\n", err);
                        }
#endif
                        lte_lc_normal();
                }
        }
}

static void work_init(void)
{

        k_work_init_delayable(&server_transmission_work,
                              server_transmission_work_fn);
        k_work_init_delayable(&lte_set_connection_work,
                              lte_set_connection_work_fn);
}

void main(void)
{
        int err;
        printk("UDP sample has started\n");

        button_init();
        PSM_Enable = gpio_pin_get_dt(&buttons[2]);
        RAI_Enable = gpio_pin_get_dt(&buttons[3]);
        PSM_Enable_pr = PSM_Enable;
        RAI_Enable_pr = RAI_Enable;

        LTE_Connection_Current_State = LTE_STATE_BUSY;
        LTE_Connection_Target_State = LTE_STATE_ON;

#if defined(CONFIG_NRF_MODEM_LIB)
        /* Initialize the modem before calling configure_low_power(). This is
         * because the enabling of RAI is dependent on the
         * configured network mode which is set during modem initialization.
         */
        modem_init();
        err = configure_low_power();
        if (err)
        {
                printk("Unable to set low power configuration, error: %d\n",
                       err);
        }
        modem_connect();
#endif
        while (LTE_STATE_BUSY == LTE_Connection_Current_State)
        {
                printk("lte_set_connection BUSY!\n");
                k_sleep(K_SECONDS(3));
        }
        err = server_init();
        if (err)
        {
                printk("Not able to initialize UDP server connection\n");
                return;
        }

        work_init();
        k_work_schedule(&server_transmission_work, K_NO_WAIT);
}
