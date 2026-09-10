/*
 * Handoff — BTstack build configuration. Development plan M2.
 *
 * BTstack is configured at compile time by this header; the SDK puts whatever
 * `btstack_config.h` it finds on the include path into the stack's build. It
 * lives under hal_pico/ because it is as hardware-bound as anything here, and
 * because that directory is already a PUBLIC include directory of handoff_lib.
 *
 * The flow-control and buffer settings are the Pico W port's, and the reason
 * they are not left at BTstack's defaults is specific: the CYW43439 shares one
 * SPI bus between wireless and Bluetooth, and without host-to-controller flow
 * control that bus overruns.
 *
 * What is ours rather than the port's, and why:
 *
 *   ENABLE_LE_SECURE_CONNECTIONS  architecture §11.2 requires my_vcard and
 *                                 rx_vcard to need a bonded, encrypted link,
 *                                 so a bystander can neither read the wearer's
 *                                 card nor inject one.
 *   NVM_NUM_DEVICE_DB_ENTRIES     the bond has to survive a Pico power cycle
 *                                 (M2 exit criterion) so the LE device DB is
 *                                 kept in flash via pico_btstack_flash_bank.
 *   MAX_NR_HCI_CONNECTIONS 1      a wristband talks to one phone.
 */
#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

/* ---- features --------------------------------------------------------- */

/* pico_btstack_ble already defines this on the command line; defining it
 * again here is a warning, and leaving it out entirely would make this file
 * depend on which SDK library happened to be linked. */
#ifndef ENABLE_BLE
#define ENABLE_BLE
#endif

#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_SECURE_CONNECTIONS
#define ENABLE_LE_DATA_LENGTH_EXTENSION

/*
 * Do the P-256 work in software rather than over HCI. It is slower — a couple
 * of seconds for the one pairing that ever happens — and it keeps the key
 * exchange off the shared CYW43 bus, which is the part of this board most
 * likely to misbehave under load.
 */
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS
#define ENABLE_SOFTWARE_AES128

#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP

/* ---- buffers and sizes ------------------------------------------------ */

#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (255 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

#define MAX_NR_HCI_CONNECTIONS 1
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4

/* The CYW43439 shares one bus between WiFi and Bluetooth. Both of these exist
 * to stop it being overrun; they are the Pico W port's values, not ours. */
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define HCI_HOST_ACL_PACKET_LEN (255 + 4)
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

/* Bonds live in flash, via pico_btstack_flash_bank. Four is generous for a
 * device that pairs with its wearer's phone and nothing else. */
#define NVM_NUM_DEVICE_DB_ENTRIES 4
#define NVM_NUM_LINK_KEYS 4

/* No malloc is given to BTstack, so the ATT database is a fixed array. */
#define MAX_ATT_DB_SIZE 512

/* ---- platform --------------------------------------------------------- */

#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT

#define HCI_RESET_RESEND_TIMEOUT_MS 1000

#endif /* BTSTACK_CONFIG_H */
