#include "mtp_state.h"

#include <string.h>

#include "tusb.h"

typedef struct {
    mtp_session_state_t session_state;
    uint32_t session_id;
    mtp_upload_state_t upload_state;
    mtp_upload_info_t upload_info;
    uint16_t deferred_response;
    bool object_added_pending;
    mtp_object_added_event_t object_added_event;
} mtp_state_context_t;

static mtp_state_context_t s_state = {
    .deferred_response = MTP_RESP_OK,
};

void mtp_state_reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.deferred_response = MTP_RESP_OK;
}

mtp_session_state_t mtp_state_get_session_state(void)
{
    return s_state.session_state;
}

bool mtp_state_open_session(uint32_t session_id)
{
    if (s_state.session_state == MTP_SESSION_STATE_OPEN) {
        return false;
    }
    s_state.session_state = MTP_SESSION_STATE_OPEN;
    s_state.session_id = session_id;
    return true;
}

bool mtp_state_close_session(void)
{
    if (s_state.session_state == MTP_SESSION_STATE_CLOSED) {
        return false;
    }
    s_state.session_state = MTP_SESSION_STATE_CLOSED;
    s_state.session_id = 0;
    return true;
}

uint32_t mtp_state_get_session_id(void)
{
    return s_state.session_id;
}

mtp_upload_state_t mtp_state_get_upload_state(void)
{
    return s_state.upload_state;
}

void mtp_state_begin_upload_info(void)
{
    memset(&s_state.upload_info, 0, sizeof(s_state.upload_info));
    s_state.upload_state = MTP_UPLOAD_STATE_INFO_RECEIVING;
    s_state.deferred_response = MTP_RESP_OK;
}

bool mtp_state_set_upload_info(const mtp_upload_info_t *info)
{
    if (info == NULL || info->handle == 0 || s_state.upload_state != MTP_UPLOAD_STATE_INFO_RECEIVING) {
        return false;
    }
    s_state.upload_info = *info;
    s_state.upload_state = MTP_UPLOAD_STATE_INFO_READY;
    return true;
}

bool mtp_state_get_upload_info(mtp_upload_info_t *info)
{
    if (info == NULL || (s_state.upload_state != MTP_UPLOAD_STATE_INFO_READY && s_state.upload_state != MTP_UPLOAD_STATE_DATA_RECEIVING)) {
        return false;
    }
    *info = s_state.upload_info;
    return true;
}

bool mtp_state_begin_upload_data(void)
{
    if (s_state.upload_state != MTP_UPLOAD_STATE_INFO_READY) {
        return false;
    }
    s_state.upload_state = MTP_UPLOAD_STATE_DATA_RECEIVING;
    return true;
}

void mtp_state_discard_upload(void)
{
    memset(&s_state.upload_info, 0, sizeof(s_state.upload_info));
    s_state.upload_state = MTP_UPLOAD_STATE_IDLE;
}

void mtp_state_set_deferred_response(uint16_t response)
{
    s_state.deferred_response = response;
}

uint16_t mtp_state_get_deferred_response(void)
{
    return s_state.deferred_response;
}

void mtp_state_queue_object_added(uint32_t handle, uint32_t transaction_id)
{
    s_state.object_added_event.handle = handle;
    s_state.object_added_event.transaction_id = transaction_id;
    s_state.object_added_pending = true;
}

bool mtp_state_get_pending_object_added(mtp_object_added_event_t *event)
{
    if (event == NULL || !s_state.object_added_pending) {
        return false;
    }
    *event = s_state.object_added_event;
    return true;
}

void mtp_state_complete_object_added(void)
{
    memset(&s_state.object_added_event, 0, sizeof(s_state.object_added_event));
    s_state.object_added_pending = false;
}
