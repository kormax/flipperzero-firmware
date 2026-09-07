#include "nfc_cli_dump_ask_cts.h"
#include <nfc/protocols/ask_cts/ask_cts_poller.h>

NfcCommand nfc_cli_dump_poller_callback_ask_cts(NfcGenericEvent event, void* context) {
    furi_assert(event.protocol == NfcProtocolAskCts);

    NfcCliDumpContext* instance = context;
    const AskCtsPollerEvent* ask_cts_event = event.event_data;

    NfcCommand command = NfcCommandContinue;

    if(ask_cts_event->type == AskCtsPollerEventTypeReadSuccess) {
        nfc_device_set_data(
            instance->nfc_device, NfcProtocolAskCts, nfc_poller_get_data(instance->poller));
        instance->result = NfcCliDumpErrorNone;
        command = NfcCommandStop;
    } else if(ask_cts_event->type == AskCtsPollerEventTypeReadFailed) {
        instance->result = NfcCliDumpErrorFailedToRead;
        command = NfcCommandStop;
    }

    if(command == NfcCommandStop) {
        furi_semaphore_release(instance->sem_done);
    }

    return command;
}
