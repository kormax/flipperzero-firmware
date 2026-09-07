#include "ask_cts_poller.h"
#include "ask_cts_poller_defs.h"

#include <furi.h>
#include <nfc/helpers/iso14443_crc.h>
#include <nfc/nfc_poller.h>

#define ASK_CTS_CMD_REQT           (0x10U)
#define ASK_CTS_CMD_SELECT_ALL     (0x9FU)
#define ASK_CTS_CMD_READ_UID       (0xC4U)
#define ASK_CTS_CMD_READ           (0xC0U)
#define ASK_CTS_BLOCK_ADDRESS_MASK (0x1FU)

#define ASK_CTS_GUARD_TIME_US      (5000U)
#define ASK_CTS_FDT_POLL_FC        (9000U)
#define ASK_CTS_FWT_FC             (60000U)
#define ASK_CTS_POLL_POLL_MIN_US   (1280U)
#define ASK_CTS_POLLER_BUFFER_SIZE (8U)

typedef enum {
    AskCtsPollerStateSelect,
    AskCtsPollerStateRead,
    AskCtsPollerStateReadSuccess,
    AskCtsPollerStateReadFailed,

    AskCtsPollerStateNum,
} AskCtsPollerState;

struct AskCtsPoller {
    Nfc* nfc;
    BitBuffer* tx_buffer;
    BitBuffer* rx_buffer;
    AskCtsData* data;
    AskCtsPollerState state;
    uint8_t current_block;

    NfcGenericCallback callback;
    void* context;
    NfcGenericEvent general_event;
    AskCtsPollerEvent ask_cts_event;
    AskCtsPollerEventData ask_cts_event_data;
};

static AskCtsError ask_cts_poller_exchange(
    AskCtsPoller* instance,
    const uint8_t* command,
    size_t command_size,
    uint8_t response[ASK_CTS_BLOCK_SIZE]) {
    bit_buffer_copy_bytes(instance->tx_buffer, command, command_size);
    iso14443_crc_append(Iso14443CrcTypeB, instance->tx_buffer);
    bit_buffer_reset(instance->rx_buffer);

    const NfcError error =
        nfc_poller_trx(instance->nfc, instance->tx_buffer, instance->rx_buffer, ASK_CTS_FWT_FC);
    if(error != NfcErrorNone) {
        return error == NfcErrorTimeout ? AskCtsErrorTimeout : AskCtsErrorNotPresent;
    }
    const size_t rx_size = bit_buffer_get_size_bytes(instance->rx_buffer);
    if(rx_size != ASK_CTS_BLOCK_SIZE + ISO14443_CRC_SIZE) {
        return AskCtsErrorCommunication;
    }
    if(!iso14443_crc_check(Iso14443CrcTypeB, instance->rx_buffer)) {
        return AskCtsErrorWrongCrc;
    }

    memcpy(response, bit_buffer_get_data(instance->rx_buffer), ASK_CTS_BLOCK_SIZE);
    return AskCtsErrorNone;
}

static AskCtsError ask_cts_poller_activate(AskCtsPoller* instance, AskCtsData* data) {
    const uint8_t reqt[] = {ASK_CTS_CMD_REQT};
    const uint8_t select_all[] = {ASK_CTS_CMD_SELECT_ALL, 0xFFU, 0xFFU};
    const uint8_t read_uid[] = {ASK_CTS_CMD_READ_UID};
    uint8_t response[ASK_CTS_BLOCK_SIZE];
    AskCtsData result = {};

    AskCtsError error = ask_cts_poller_exchange(instance, reqt, sizeof(reqt), response);
    if(error == AskCtsErrorNone) {
        result.product_code = response[0];
        result.fab_code = response[1];
        error = ask_cts_poller_exchange(instance, select_all, sizeof(select_all), result.uid);
    }
    if(error == AskCtsErrorNone) {
        error = ask_cts_poller_exchange(instance, read_uid, sizeof(read_uid), &result.uid[2]);
    }
    if(error == AskCtsErrorNone) {
        *data = result;
    }

    return error;
}

static uint8_t ask_cts_poller_get_block_count(uint8_t product_code) {
    switch(product_code) {
    case ASK_CTS_PRODUCT_CTS256B:
        return ASK_CTS_CTS256B_BLOCK_COUNT;
    case ASK_CTS_PRODUCT_CTS512B:
        return ASK_CTS_CTS512B_BLOCK_COUNT;
    default:
        return 0U;
    }
}

static const AskCtsData* ask_cts_poller_get_data(AskCtsPoller* instance) {
    furi_assert(instance);
    return instance->data;
}

static AskCtsPoller* ask_cts_poller_alloc(Nfc* nfc) {
    furi_assert(nfc);

    AskCtsPoller* instance = malloc(sizeof(AskCtsPoller));
    instance->nfc = nfc;
    instance->tx_buffer = bit_buffer_alloc(ASK_CTS_POLLER_BUFFER_SIZE);
    instance->rx_buffer = bit_buffer_alloc(ASK_CTS_POLLER_BUFFER_SIZE);
    instance->data = malloc(sizeof(AskCtsData));
    instance->state = AskCtsPollerStateSelect;

    nfc_config(instance->nfc, NfcModePoller, NfcTechAskCts);
    nfc_set_guard_time_us(instance->nfc, ASK_CTS_GUARD_TIME_US);
    nfc_set_fdt_poll_fc(instance->nfc, ASK_CTS_FDT_POLL_FC);
    nfc_set_fdt_poll_poll_us(instance->nfc, ASK_CTS_POLL_POLL_MIN_US);

    instance->ask_cts_event.data = &instance->ask_cts_event_data;
    instance->general_event.protocol = NfcProtocolAskCts;
    instance->general_event.event_data = &instance->ask_cts_event;
    instance->general_event.instance = instance;

    return instance;
}

static void ask_cts_poller_free(AskCtsPoller* instance) {
    furi_assert(instance);
    bit_buffer_free(instance->tx_buffer);
    bit_buffer_free(instance->rx_buffer);
    free(instance->data);
    free(instance);
}

static void ask_cts_poller_set_callback(
    AskCtsPoller* instance,
    NfcGenericCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(callback);
    instance->callback = callback;
    instance->context = context;
}

typedef NfcCommand (*AskCtsPollerStateHandler)(AskCtsPoller* instance);

static NfcCommand ask_cts_poller_select_handler(AskCtsPoller* instance) {
    const AskCtsError error = ask_cts_poller_activate(instance, instance->data);
    if(error == AskCtsErrorNone) {
        instance->data->block_count = ask_cts_poller_get_block_count(instance->data->product_code);
        instance->current_block = 0;
        instance->state = AskCtsPollerStateRead;
    } else {
        instance->ask_cts_event_data.error = error;
        instance->state = AskCtsPollerStateReadFailed;
    }

    return NfcCommandContinue;
}

static NfcCommand ask_cts_poller_read_handler(AskCtsPoller* instance) {
    const uint8_t block = instance->current_block;
    if(block == instance->data->block_count) {
        instance->state = AskCtsPollerStateReadSuccess;
        return NfcCommandContinue;
    }

    const uint8_t command = ASK_CTS_CMD_READ | (block & ASK_CTS_BLOCK_ADDRESS_MASK);
    const AskCtsError error = ask_cts_poller_exchange(
        instance, &command, sizeof(command), instance->data->blocks[block]);
    if(error == AskCtsErrorNone) {
        instance->current_block++;
    } else {
        instance->ask_cts_event_data.error = error;
        instance->state = AskCtsPollerStateReadFailed;
    }

    return NfcCommandContinue;
}

static NfcCommand ask_cts_poller_read_success_handler(AskCtsPoller* instance) {
    instance->ask_cts_event.type = AskCtsPollerEventTypeReadSuccess;
    const NfcCommand command = instance->callback(instance->general_event, instance->context);
    instance->state = AskCtsPollerStateSelect;

    return command;
}

static NfcCommand ask_cts_poller_read_failed_handler(AskCtsPoller* instance) {
    instance->ask_cts_event.type = AskCtsPollerEventTypeReadFailed;
    const NfcCommand command = instance->callback(instance->general_event, instance->context);
    furi_delay_ms(100);
    instance->state = AskCtsPollerStateSelect;

    return command;
}

static const AskCtsPollerStateHandler ask_cts_poller_state_handlers[AskCtsPollerStateNum] = {
    [AskCtsPollerStateSelect] = ask_cts_poller_select_handler,
    [AskCtsPollerStateRead] = ask_cts_poller_read_handler,
    [AskCtsPollerStateReadSuccess] = ask_cts_poller_read_success_handler,
    [AskCtsPollerStateReadFailed] = ask_cts_poller_read_failed_handler,
};

static NfcCommand ask_cts_poller_run(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.protocol == NfcProtocolInvalid);
    furi_assert(event.event_data);

    AskCtsPoller* instance = context;
    const NfcEvent* nfc_event = event.event_data;
    NfcCommand command = NfcCommandContinue;

    furi_assert(instance->state < AskCtsPollerStateNum);

    if(nfc_event->type == NfcEventTypePollerReady) {
        command = ask_cts_poller_state_handlers[instance->state](instance);
    }

    return command;
}

static bool ask_cts_poller_detect(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.protocol == NfcProtocolInvalid);

    const NfcEvent* nfc_event = event.event_data;
    if(nfc_event->type != NfcEventTypePollerReady) return false;

    AskCtsData data;
    return ask_cts_poller_activate(context, &data) == AskCtsErrorNone;
}

const NfcPollerBase nfc_poller_ask_cts = {
    .alloc = (NfcPollerAlloc)ask_cts_poller_alloc,
    .free = (NfcPollerFree)ask_cts_poller_free,
    .set_callback = (NfcPollerSetCallback)ask_cts_poller_set_callback,
    .run = (NfcPollerRun)ask_cts_poller_run,
    .detect = (NfcPollerDetect)ask_cts_poller_detect,
    .get_data = (NfcPollerGetData)ask_cts_poller_get_data,
};
