#define COMMS_TASK_NAME   ("comms")
#define COMMS_TASK_PRIO   (1)
#define COMMS_TASK_STK_SZ (1024*4)

#define HOMEKIT_TASK_NAME   ("homekit")
#define HOMEKIT_TASK_PRIO   (2)
#define HOMEKIT_TASK_STK_SZ (1024*4)

#define WIFI_TASK_NAME   ("wifi")
#define WIFI_TASK_PRIO   (1)
#define WIFI_TASK_STK_SZ (4 * 1024)

#define CLI_TASK_NAME   ("cli")
#define CLI_TASK_PRIO   (3)
#define CLI_TASK_STK_SZ (4 * 1024)

#define RX_ISR_TASK_NAME ("rx_isr")
#define RX_ISR_TASK_PRIO (8)
#define RX_ISR_TASK_STK_SZ (4 * 1024)
