#pragma once

#include <nfc/protocols/nfc_device_base_i.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASK_CTS_UID_SIZE        (4U)
#define ASK_CTS_BLOCK_SIZE      (2U)
#define ASK_CTS_MAX_BLOCK_COUNT (32U)

#define ASK_CTS_PRODUCT_CTS256B (0x50U)
#define ASK_CTS_PRODUCT_CTS512B (0x60U)

#define ASK_CTS_CTS256B_BLOCK_COUNT (16U)
#define ASK_CTS_CTS512B_BLOCK_COUNT (32U)

typedef enum {
    AskCtsErrorNone,
    AskCtsErrorCommunication,
    AskCtsErrorWrongCrc,
    AskCtsErrorTimeout,
    AskCtsErrorNotPresent,
} AskCtsError;

typedef struct {
    uint8_t uid[ASK_CTS_UID_SIZE];
    uint8_t product_code;
    uint8_t fab_code;
    uint8_t block_count;
    uint8_t blocks[ASK_CTS_MAX_BLOCK_COUNT][ASK_CTS_BLOCK_SIZE];
} AskCtsData;

extern const NfcDeviceBase nfc_device_ask_cts;

#ifdef __cplusplus
}
#endif
