#include "ask_cts_poller.h"
#include "ask_cts_poller_defs.h"

#include <furi.h>
#include <nfc/helpers/iso14443_crc.h>
#include <nfc/nfc_poller.h>

#define ASK_CTS_CMD_REQT       (0x10U)
#define ASK_CTS_CMD_SELECT_ALL (0x9FU)
#define ASK_CTS_CMD_READ_UID   (0xC4U)

#define ASK_CTS_GUARD_TIME_US      (5000U)
#define ASK_CTS_FDT_POLL_FC        (9000U)
#define ASK_CTS_FWT_FC             (60000U)
#define ASK_CTS_POLL_POLL_MIN_US   (1280U)
#define ASK_CTS_POLLER_BUFFER_SIZE (8U)

struct AskCtsPoller {
    Nfc* nfc;
    BitBuffer* tx_buffer;
    BitBuffer* rx_buffer;
    AskCtsData* data;
    bool activated;

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
    uint8_t response[2]) {
    bit_buffer_copy_bytes(instance->tx_buffer, command, command_size);
    iso14443_crc_append(Iso14443CrcTypeB, instance->tx_buffer);
    bit_buffer_reset(instance->rx_buffer);

    const NfcError error =
        nfc_poller_trx(instance->nfc, instance->tx_buffer, instance->rx_buffer, ASK_CTS_FWT_FC);
    if(error != NfcErrorNone) {
        return error == NfcErrorTimeout ? AskCtsErrorTimeout : AskCtsErrorNotPresent;
    }
    const size_t rx_size = bit_buffer_get_size_bytes(instance->rx_buffer);
    if(rx_size != 4U) {
        return AskCtsErrorCommunication;
    }
    if(!iso14443_crc_check(Iso14443CrcTypeB, instance->rx_buffer)) {
        return AskCtsErrorWrongCrc;
    }

    memcpy(response, bit_buffer_get_data(instance->rx_buffer), 2U);
    return AskCtsErrorNone;
}

static AskCtsError ask_cts_poller_activate(AskCtsPoller* instance, AskCtsData* data) {
    if(instance->activated) {
        *data = *instance->data;
        return AskCtsErrorNone;
    }

    const uint8_t reqt[] = {ASK_CTS_CMD_REQT};
    const uint8_t select_all[] = {ASK_CTS_CMD_SELECT_ALL, 0xFFU, 0xFFU};
    const uint8_t read_uid[] = {ASK_CTS_CMD_READ_UID};
    uint8_t response[2];
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
        *instance->data = result;
        instance->activated = true;
    }

    return error;
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
    instance->activated = false;

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

static NfcCommand ask_cts_poller_run(NfcGenericEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.protocol == NfcProtocolInvalid);

    AskCtsPoller* instance = context;
    const NfcEvent* nfc_event = event.event_data;
    if(nfc_event->type != NfcEventTypePollerReady) return NfcCommandContinue;

    const AskCtsError error = ask_cts_poller_activate(instance, instance->data);
    if(error == AskCtsErrorNone) {
        instance->ask_cts_event.type = AskCtsPollerEventTypeReady;
    } else {
        instance->ask_cts_event.type = AskCtsPollerEventTypeError;
        instance->ask_cts_event_data.error = error;
    }
    const NfcCommand command = instance->callback(instance->general_event, instance->context);
    if(error != AskCtsErrorNone) furi_delay_ms(100);
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
