#pragma once

#include <nfc/protocols/nfc_device_base_i.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASK_CTS_UID_SIZE (4U)

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
} AskCtsData;

extern const NfcDeviceBase nfc_device_ask_cts;

#ifdef __cplusplus
}
#endif
