#pragma once

#include "ask_cts.h"

#include <nfc/protocols/nfc_generic_event.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AskCtsPoller AskCtsPoller;

typedef enum {
    AskCtsPollerEventTypeReadFailed,
    AskCtsPollerEventTypeReadSuccess,
} AskCtsPollerEventType;

typedef union {
    AskCtsError error;
} AskCtsPollerEventData;

typedef struct {
    AskCtsPollerEventType type;
    AskCtsPollerEventData* data;
} AskCtsPollerEvent;

#ifdef __cplusplus
}
#endif
