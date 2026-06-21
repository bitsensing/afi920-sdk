/**
 * @file bts_iso23150_someip.h
 * @brief SOME/IP header serialization for ISO 23150
 */

#ifndef BTS_ISO23150_SOMEIP_H
#define BTS_ISO23150_SOMEIP_H

#include "bts_iso23150.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Transport Mode (controls TP segmentation behavior)
 *============================================================================*/

typedef enum {
    BTS_SOMEIP_TRANSPORT_UDP,   /**< UDP: apply TP when payload > 1392B */
    BTS_SOMEIP_TRANSPORT_TCP    /**< TCP: never apply TP (single frame) */
} bts_someip_transport_t;

/*============================================================================
 * SOME/IP Header Structure
 *============================================================================*/

typedef struct {
    uint16_t service_id;
    uint16_t event_id;
    uint16_t client_id;
    uint16_t session_id;
    uint8_t  protocol_version;
    uint8_t  interface_version;
    uint8_t  message_type;
    uint8_t  return_code;
} bts_someip_header_t;

/*============================================================================
 * SOME/IP TP (Transport Protocol) Header
 *============================================================================*/

typedef struct {
    uint32_t offset_bytes;      /* Offset in bytes (must be multiple of 16) */
    bool     more_segments;     /* True if more segments follow */
} bts_someip_tp_header_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Initialize SOME/IP header with default values
 * @param header Pointer to header structure
 * @param event_id Event ID (e.g., BTS_SOMEIP_EVENT_RDI)
 */
void bts_someip_header_init(bts_someip_header_t* header, uint16_t event_id);

/**
 * @brief Serialize SOME/IP header to buffer
 * @param header Pointer to header structure
 * @param payload_size Size of payload in bytes
 * @param is_tp True if TP header is used
 * @param buffer Output buffer (must be at least BTS_SOMEIP_HEADER_SIZE bytes)
 * @param buffer_size Size of output buffer
 * @return Number of bytes written, or negative error code
 */
int bts_someip_header_serialize(
    const bts_someip_header_t* header,
    uint32_t payload_size,
    bool is_tp,
    uint8_t* buffer,
    size_t buffer_size);

/**
 * @brief Serialize SOME/IP TP header to buffer
 * @param tp Pointer to TP header structure
 * @param buffer Output buffer (must be at least BTS_SOMEIP_TP_HEADER_SIZE bytes)
 * @param buffer_size Size of output buffer
 * @return Number of bytes written, or negative error code
 */
int bts_someip_tp_header_serialize(
    const bts_someip_tp_header_t* tp,
    uint8_t* buffer,
    size_t buffer_size);

/**
 * @brief Callback function for sending data
 * @param data Data to send
 * @param size Size of data
 * @param user_data User-provided context
 * @return 0 on success, negative on error
 */
typedef int (*bts_send_callback_t)(const uint8_t* data, size_t size, void* user_data);

/**
 * @brief Send payload with SOME/IP header
 *
 * In UDP mode, payloads exceeding BTS_SOMEIP_TP_CHUNK_SIZE are automatically
 * segmented using SOME/IP-TP. In TCP mode, TP is never applied — the entire
 * payload is sent as a single SOME/IP frame (TCP handles segmentation).
 *
 * @param event_id Event ID
 * @param payload Payload data
 * @param payload_size Size of payload
 * @param transport Transport mode (UDP: TP auto, TCP: no TP)
 * @param session_id Pointer to session ID (will be incremented)
 * @param send_cb Callback function for sending
 * @param user_data User context for callback
 * @return BTS_ISO23150_OK on success, or negative error code
 */
int bts_someip_send(
    uint16_t event_id,
    const uint8_t* payload,
    size_t payload_size,
    bts_someip_transport_t transport,
    uint16_t* session_id,
    bts_send_callback_t send_cb,
    void* user_data);

/*============================================================================
 * Zero-Copy Scatter-Gather API
 *============================================================================*/

/**
 * @brief Scatter-gather I/O vector (POSIX-independent)
 */
typedef struct {
    const uint8_t *base;
    size_t         len;
} bts_iov_t;

/**
 * @brief Scatter-gather send callback
 * @param iov     Array of I/O vectors to send as a single datagram
 * @param iovcnt  Number of vectors
 * @param user_data User-provided context
 * @return 0 on success, negative on error
 */
typedef int (*bts_sendv_callback_t)(const bts_iov_t* iov, int iovcnt, void* user_data);

/**
 * @brief Zero-copy send with scatter-gather payload
 *
 * Unlike bts_someip_send() which requires a contiguous payload buffer,
 * this function accepts a scatter list of payload regions and avoids
 * memcpy by building iovec chains pointing directly to source data.
 *
 * In UDP mode, payloads exceeding BTS_SOMEIP_TP_CHUNK_SIZE are automatically
 * segmented using SOME/IP-TP. In TCP mode, TP is never applied.
 *
 * @param event_id           Event ID
 * @param payload_iov        Scatter list of payload regions
 * @param payload_iovcnt     Number of payload regions
 * @param total_payload_size Sum of all payload_iov[].len
 * @param transport          Transport mode (UDP: TP auto, TCP: no TP)
 * @param session_id         Pointer to session ID (will be incremented)
 * @param sendv_cb           Scatter-gather send callback
 * @param user_data          User context for callback
 * @return BTS_ISO23150_OK on success, or negative error code
 */
int bts_someip_sendv(
    uint16_t event_id,
    const bts_iov_t* payload_iov,
    int payload_iovcnt,
    size_t total_payload_size,
    bts_someip_transport_t transport,
    uint16_t* session_id,
    bts_sendv_callback_t sendv_cb,
    void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* BTS_ISO23150_SOMEIP_H */
