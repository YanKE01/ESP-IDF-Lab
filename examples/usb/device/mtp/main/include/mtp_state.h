#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MTP_SESSION_STATE_CLOSED,
    MTP_SESSION_STATE_OPEN,
} mtp_session_state_t;

typedef enum {
    MTP_UPLOAD_STATE_IDLE,
    MTP_UPLOAD_STATE_INFO_RECEIVING,
    MTP_UPLOAD_STATE_INFO_READY,
    MTP_UPLOAD_STATE_DATA_RECEIVING,
} mtp_upload_state_t;

typedef struct {
    uint32_t handle;
    uint32_t parent;
    uint32_t size;
} mtp_upload_info_t;

typedef struct {
    uint32_t handle;
    uint32_t transaction_id;
} mtp_object_added_event_t;

/**
 * @brief Resets all MTP application state.
 */
void mtp_state_reset(void);

/**
 * @brief Returns the current MTP session state.
 */
mtp_session_state_t mtp_state_get_session_state(void);

/**
 * @brief Opens an MTP session.
 *
 * @return true when the session was opened, or false when one was already open.
 */
bool mtp_state_open_session(uint32_t session_id);

/**
 * @brief Closes the current MTP session.
 *
 * @return true when a session was closed, or false when none was open.
 */
bool mtp_state_close_session(void);

/**
 * @brief Returns the current session identifier.
 */
uint32_t mtp_state_get_session_id(void);

/**
 * @brief Returns the current object upload state.
 */
mtp_upload_state_t mtp_state_get_upload_state(void);

/**
 * @brief Starts receiving a SendObjectInfo dataset.
 */
void mtp_state_begin_upload_info(void);

/**
 * @brief Stores validated object information and makes it ready for SendObject.
 */
bool mtp_state_set_upload_info(const mtp_upload_info_t *info);

/**
 * @brief Returns the current upload information when it is available.
 */
bool mtp_state_get_upload_info(mtp_upload_info_t *info);

/**
 * @brief Moves a prepared upload into the object data receiving state.
 */
bool mtp_state_begin_upload_data(void);

/**
 * @brief Discards the current upload state and its metadata.
 */
void mtp_state_discard_upload(void);

/**
 * @brief Saves a response code until the current data phase completes.
 */
void mtp_state_set_deferred_response(uint16_t response);

/**
 * @brief Returns the response code saved for the current data phase.
 */
uint16_t mtp_state_get_deferred_response(void);

/**
 * @brief Queues an ObjectAdded event for the response completion callback.
 */
void mtp_state_queue_object_added(uint32_t handle, uint32_t transaction_id);

/**
 * @brief Returns the queued ObjectAdded event without removing it.
 */
bool mtp_state_get_pending_object_added(mtp_object_added_event_t *event);

/**
 * @brief Marks the queued ObjectAdded event as sent.
 */
void mtp_state_complete_object_added(void);
