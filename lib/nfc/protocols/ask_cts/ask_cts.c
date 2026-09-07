#include "ask_cts.h"

#include <furi.h>
#include <nfc/nfc_common.h>

#define ASK_CTS_PROTOCOL_NAME   "ASK CTS"
#define ASK_CTS_PRODUCT_KEY     "Product Code"
#define ASK_CTS_FAB_KEY         "Fab Code"
#define ASK_CTS_BLOCK_COUNT_KEY "Blocks total"
#define ASK_CTS_BLOCK_KEY       "Block %u"

static AskCtsData* ask_cts_alloc(void) {
    return malloc(sizeof(AskCtsData));
}

static void ask_cts_free(AskCtsData* data) {
    furi_check(data);
    free(data);
}

static void ask_cts_reset(AskCtsData* data) {
    furi_check(data);
    memset(data, 0, sizeof(AskCtsData));
}

static void ask_cts_copy(AskCtsData* data, const AskCtsData* other) {
    furi_check(data);
    furi_check(other);
    *data = *other;
}

static bool ask_cts_verify(AskCtsData* data, const FuriString* device_type) {
    UNUSED(data);
    furi_check(device_type);
    return furi_string_equal_str(device_type, ASK_CTS_PROTOCOL_NAME);
}

static bool ask_cts_load(AskCtsData* data, FlipperFormat* ff, uint32_t version) {
    furi_check(data);
    furi_check(ff);

    bool parsed = false;
    FuriString* key = furi_string_alloc();
    uint32_t block_count = 0;

    do {
        if(version < NFC_UNIFIED_FORMAT_VERSION) break;
        if(!flipper_format_read_hex(ff, ASK_CTS_PRODUCT_KEY, &data->product_code, 1)) break;
        if(!flipper_format_read_hex(ff, ASK_CTS_FAB_KEY, &data->fab_code, 1)) break;
        if(!flipper_format_read_uint32(ff, ASK_CTS_BLOCK_COUNT_KEY, &block_count, 1)) break;
        if(block_count > ASK_CTS_MAX_BLOCK_COUNT) break;
        data->block_count = block_count;

        bool blocks_parsed = true;
        for(uint8_t block = 0; block < data->block_count; block++) {
            furi_string_printf(key, ASK_CTS_BLOCK_KEY, block);
            if(!flipper_format_read_hex(
                   ff, furi_string_get_cstr(key), data->blocks[block], ASK_CTS_BLOCK_SIZE)) {
                blocks_parsed = false;
                break;
            }
        }
        if(!blocks_parsed) break;

        parsed = true;
    } while(false);

    furi_string_free(key);
    return parsed;
}

static bool ask_cts_save(const AskCtsData* data, FlipperFormat* ff) {
    furi_check(data);
    furi_check(ff);

    bool saved = false;
    FuriString* key = furi_string_alloc();
    const uint32_t block_count = data->block_count;

    do {
        if(!flipper_format_write_comment_cstr(ff, ASK_CTS_PROTOCOL_NAME " specific data")) break;
        if(!flipper_format_write_hex(ff, ASK_CTS_PRODUCT_KEY, &data->product_code, 1)) break;
        if(!flipper_format_write_hex(ff, ASK_CTS_FAB_KEY, &data->fab_code, 1)) break;
        if(!flipper_format_write_uint32(ff, ASK_CTS_BLOCK_COUNT_KEY, &block_count, 1)) break;

        bool blocks_saved = true;
        for(uint8_t block = 0; block < data->block_count; block++) {
            furi_string_printf(key, ASK_CTS_BLOCK_KEY, block);
            if(!flipper_format_write_hex(
                   ff, furi_string_get_cstr(key), data->blocks[block], ASK_CTS_BLOCK_SIZE)) {
                blocks_saved = false;
                break;
            }
        }
        if(!blocks_saved) break;

        saved = true;
    } while(false);

    furi_string_free(key);
    return saved;
}

static bool ask_cts_is_equal(const AskCtsData* data, const AskCtsData* other) {
    furi_check(data);
    furi_check(other);
    return memcmp(data, other, sizeof(AskCtsData)) == 0;
}

static const char* ask_cts_get_device_name(const AskCtsData* data, NfcDeviceNameType name_type) {
    furi_check(data);

    if(name_type != NfcDeviceNameTypeFull) return "ASK CTS";
    if(data->product_code == ASK_CTS_PRODUCT_CTS256B) return "ASK CTS256B";
    if(data->product_code == ASK_CTS_PRODUCT_CTS512B) return "ASK CTS512B";
    return "ASK CTS";
}

static const uint8_t* ask_cts_get_uid(const AskCtsData* data, size_t* uid_len) {
    furi_check(data);
    if(uid_len) *uid_len = ASK_CTS_UID_SIZE;
    return data->uid;
}

static bool ask_cts_set_uid(AskCtsData* data, const uint8_t* uid, size_t uid_len) {
    furi_check(data);
    furi_check(uid);

    if(uid_len != ASK_CTS_UID_SIZE) return false;
    memcpy(data->uid, uid, uid_len);
    return true;
}

static AskCtsData* ask_cts_get_base_data(const AskCtsData* data) {
    UNUSED(data);
    furi_crash("No base data");
}

const NfcDeviceBase nfc_device_ask_cts = {
    .protocol_name = ASK_CTS_PROTOCOL_NAME,
    .alloc = (NfcDeviceAlloc)ask_cts_alloc,
    .free = (NfcDeviceFree)ask_cts_free,
    .reset = (NfcDeviceReset)ask_cts_reset,
    .copy = (NfcDeviceCopy)ask_cts_copy,
    .verify = (NfcDeviceVerify)ask_cts_verify,
    .load = (NfcDeviceLoad)ask_cts_load,
    .save = (NfcDeviceSave)ask_cts_save,
    .is_equal = (NfcDeviceEqual)ask_cts_is_equal,
    .get_name = (NfcDeviceGetName)ask_cts_get_device_name,
    .get_uid = (NfcDeviceGetUid)ask_cts_get_uid,
    .set_uid = (NfcDeviceSetUid)ask_cts_set_uid,
    .get_base_data = (NfcDeviceGetBaseData)ask_cts_get_base_data,
};
