/**
 * @file bts_iso23150_someip.c
 * @brief SOME/IP header serialization implementation
 */

#include <string.h>
#include "bts_iso23150_someip.h"

/*============================================================================
 * Implementation
 *============================================================================*/

void bts_someip_header_init(bts_someip_header_t* header, uint16_t event_id)
{
    if (header == NULL) return;

    header->service_id = BTS_SOMEIP_SERVICE_ID;
    header->event_id = event_id;
    header->client_id = BTS_SOMEIP_CLIENT_ID;
    header->session_id = 1;
    header->protocol_version = BTS_SOMEIP_PROTOCOL_VERSION;
    header->interface_version = BTS_SOMEIP_INTERFACE_VERSION;
    header->message_type = BTS_SOMEIP_MSGTYPE_NOTIFICATION;
    header->return_code = BTS_SOMEIP_RETURN_OK;
}

int bts_someip_header_serialize(
    const bts_someip_header_t* header,
    uint32_t payload_size,
    bool is_tp,
    uint8_t* buffer,
    size_t buffer_size)
{
    if (header == NULL || buffer == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    if (buffer_size < BTS_SOMEIP_HEADER_SIZE) {
        return BTS_ISO23150_ERR_BUFFER_TOO_SMALL;
    }

    /* Message ID = ServiceID(16) | EventID(16) */
    uint32_t message_id = ((uint32_t)header->service_id << 16) | header->event_id;

    /* Length = 8 + payload_size (includes RequestID to ReturnCode) */
    uint32_t length = 8 + payload_size;

    /* Request ID = ClientID(16) | SessionID(16) */
    uint32_t request_id = ((uint32_t)header->client_id << 16) | header->session_id;

    /* Pack header (Big-endian) */
    bts_pack_be32(&buffer[0], message_id);
    bts_pack_be32(&buffer[4], length);
    bts_pack_be32(&buffer[8], request_id);

    buffer[12] = header->protocol_version;
    buffer[13] = header->interface_version;
    buffer[14] = is_tp ? (header->message_type | BTS_SOMEIP_MSGTYPE_TP_FLAG)
                       : header->message_type;
    buffer[15] = header->return_code;

    return BTS_SOMEIP_HEADER_SIZE;
}

int bts_someip_tp_header_serialize(
    const bts_someip_tp_header_t* tp,
    uint8_t* buffer,
    size_t buffer_size)
{
    if (tp == NULL || buffer == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    if (buffer_size < BTS_SOMEIP_TP_HEADER_SIZE) {
        return BTS_ISO23150_ERR_BUFFER_TOO_SMALL;
    }

    /* Validate offset is multiple of 16 */
    if ((tp->offset_bytes % BTS_SOMEIP_TP_OFFSET_UNIT) != 0) {
        return BTS_ISO23150_ERR_INVALID_PARAM;
    }

    /* Calculate offset value (in 16-byte units) */
    uint32_t offset_val = tp->offset_bytes / BTS_SOMEIP_TP_OFFSET_UNIT;

    /* Pack: [31:4] = offset, [3:1] = reserved(0), [0] = more flag */
    uint32_t segment_info = (offset_val << 4) | (tp->more_segments ? 1 : 0);

    bts_pack_be32(buffer, segment_info);

    return BTS_SOMEIP_TP_HEADER_SIZE;
}

int bts_someip_send(
    uint16_t event_id,
    const uint8_t* payload,
    size_t payload_size,
    bts_someip_transport_t transport,
    uint16_t* session_id,
    bts_send_callback_t send_cb,
    void* user_data)
{
    if (payload == NULL || session_id == NULL || send_cb == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    bts_someip_header_t header;
    bts_someip_header_init(&header, event_id);
    header.session_id = *session_id;

    /* TCP: never segment — send as single SOME/IP frame.
     * UDP: segment when payload exceeds TP chunk size. */
    if (transport == BTS_SOMEIP_TRANSPORT_TCP ||
        payload_size <= BTS_SOMEIP_TP_CHUNK_SIZE) {
        uint8_t hdr_buf[BTS_SOMEIP_HEADER_SIZE];

        int ret = bts_someip_header_serialize(&header, (uint32_t)payload_size,
                                               false, hdr_buf, sizeof(hdr_buf));
        if (ret < 0) return ret;

        if (transport == BTS_SOMEIP_TRANSPORT_TCP) {
            /* TCP (byte stream): two callbacks OK — avoids stack copy for
             * large payloads while TCP reassembly guarantees message integrity. */
            ret = send_cb(hdr_buf, BTS_SOMEIP_HEADER_SIZE, user_data);
            if (ret < 0) return ret;

            ret = send_cb(payload, payload_size, user_data);
            if (ret < 0) return ret;
        } else {
            /* UDP (datagram): header + payload must be a single atomic send.
             * payload_size is guaranteed ≤ BTS_SOMEIP_TP_CHUNK_SIZE here. */
            uint8_t buffer[BTS_SOMEIP_HEADER_SIZE + BTS_SOMEIP_TP_CHUNK_SIZE];
            memcpy(buffer, hdr_buf, BTS_SOMEIP_HEADER_SIZE);
            memcpy(buffer + BTS_SOMEIP_HEADER_SIZE, payload, payload_size);
            ret = send_cb(buffer, BTS_SOMEIP_HEADER_SIZE + payload_size, user_data);
            if (ret < 0) return ret;
        }
    }
    else {
        /* TP segmentation required */
        uint32_t offset = 0;

        while (offset < payload_size) {
            uint32_t remaining = (uint32_t)(payload_size - offset);
            uint32_t chunk_size = (remaining > BTS_SOMEIP_TP_CHUNK_SIZE)
                                  ? BTS_SOMEIP_TP_CHUNK_SIZE
                                  : remaining;

            bool more_segments = (offset + chunk_size) < payload_size;

            /* Build packet */
            uint8_t buffer[BTS_SOMEIP_HEADER_SIZE + BTS_SOMEIP_TP_HEADER_SIZE +
                          BTS_SOMEIP_TP_CHUNK_SIZE];

            /* SOME/IP header (payload_size includes TP header) */
            int ret = bts_someip_header_serialize(&header,
                        BTS_SOMEIP_TP_HEADER_SIZE + chunk_size,
                        true, buffer, sizeof(buffer));
            if (ret < 0) return ret;

            /* TP header */
            bts_someip_tp_header_t tp = {
                .offset_bytes = offset,
                .more_segments = more_segments
            };
            ret = bts_someip_tp_header_serialize(&tp,
                    &buffer[BTS_SOMEIP_HEADER_SIZE],
                    sizeof(buffer) - BTS_SOMEIP_HEADER_SIZE);
            if (ret < 0) return ret;

            /* Payload chunk */
            memcpy(&buffer[BTS_SOMEIP_HEADER_SIZE + BTS_SOMEIP_TP_HEADER_SIZE],
                   &payload[offset], chunk_size);

            /* Send */
            size_t total_size = BTS_SOMEIP_HEADER_SIZE + BTS_SOMEIP_TP_HEADER_SIZE
                               + chunk_size;
            ret = send_cb(buffer, total_size, user_data);
            if (ret < 0) return ret;

            offset += chunk_size;
        }
    }

    /* Increment session ID */
    (*session_id)++;

    return BTS_ISO23150_OK;
}

/*============================================================================
 * Zero-Copy Scatter-Gather Send
 *============================================================================*/

/**
 * @brief Extract a byte range [offset, offset+len) from a scatter list
 *        into an output iov array, without copying data.
 *
 * @param src       Source scatter list
 * @param src_cnt   Number of regions in src
 * @param offset    Byte offset into the logical payload
 * @param len       Number of bytes to extract
 * @param out       Output iov array
 * @param out_max   Maximum entries in out
 * @return Number of iov entries written, or -1 on error
 */
static int iov_slice(const bts_iov_t* src, int src_cnt,
                     size_t offset, size_t len,
                     bts_iov_t* out, int out_max)
{
    int out_idx = 0;
    size_t skip = offset;
    size_t remaining = len;

    for (int i = 0; i < src_cnt && remaining > 0; i++) {
        if (skip >= src[i].len) {
            skip -= src[i].len;
            continue;
        }

        const uint8_t *base = src[i].base + skip;
        size_t avail = src[i].len - skip;
        skip = 0;

        size_t take = (avail < remaining) ? avail : remaining;

        if (out_idx >= out_max) return -1;
        out[out_idx].base = base;
        out[out_idx].len  = take;
        out_idx++;

        remaining -= take;
    }

    return (remaining == 0) ? out_idx : -1;
}

int bts_someip_sendv(
    uint16_t event_id,
    const bts_iov_t* payload_iov,
    int payload_iovcnt,
    size_t total_payload_size,
    bts_someip_transport_t transport,
    uint16_t* session_id,
    bts_sendv_callback_t sendv_cb,
    void* user_data)
{
    if (payload_iov == NULL || session_id == NULL || sendv_cb == NULL) {
        return BTS_ISO23150_ERR_NULL_PARAM;
    }

    bts_someip_header_t header;
    bts_someip_header_init(&header, event_id);
    header.session_id = *session_id;

    /* Max iov entries: 1 (header) + payload_iovcnt regions */
    /* For TP: 1 (header+tp) + up to payload_iovcnt sliced regions */
    #define SENDV_MAX_IOV  16

    /* TCP: never segment — send as single SOME/IP frame.
     * UDP: segment when payload exceeds TP chunk size. */
    if (transport == BTS_SOMEIP_TRANSPORT_TCP ||
        total_payload_size <= BTS_SOMEIP_TP_CHUNK_SIZE) {
        /* Single packet (no TP) */
        uint8_t hdr_buf[BTS_SOMEIP_HEADER_SIZE];

        int ret = bts_someip_header_serialize(&header, (uint32_t)total_payload_size,
                                               false, hdr_buf, sizeof(hdr_buf));
        if (ret < 0) return ret;

        /* Build iov: [SOMEIP header] [payload regions...] */
        bts_iov_t iov[SENDV_MAX_IOV];
        iov[0].base = hdr_buf;
        iov[0].len  = BTS_SOMEIP_HEADER_SIZE;

        int iovcnt = 1;
        for (int i = 0; i < payload_iovcnt && iovcnt < SENDV_MAX_IOV; i++) {
            if (payload_iov[i].len > 0) {
                iov[iovcnt] = payload_iov[i];
                iovcnt++;
            }
        }

        ret = sendv_cb(iov, iovcnt, user_data);
        if (ret < 0) return ret;
    }
    else {
        /* TP segmentation required */
        uint32_t offset = 0;

        while (offset < total_payload_size) {
            uint32_t remaining = (uint32_t)(total_payload_size - offset);
            uint32_t chunk_size = (remaining > BTS_SOMEIP_TP_CHUNK_SIZE)
                                  ? BTS_SOMEIP_TP_CHUNK_SIZE
                                  : remaining;

            bool more_segments = (offset + chunk_size) < total_payload_size;

            /* Serialize SOME/IP header + TP header into stack buffer */
            uint8_t hdr_buf[BTS_SOMEIP_HEADER_SIZE + BTS_SOMEIP_TP_HEADER_SIZE];

            int ret = bts_someip_header_serialize(&header,
                        BTS_SOMEIP_TP_HEADER_SIZE + chunk_size,
                        true, hdr_buf, sizeof(hdr_buf));
            if (ret < 0) return ret;

            bts_someip_tp_header_t tp = {
                .offset_bytes = offset,
                .more_segments = more_segments
            };
            ret = bts_someip_tp_header_serialize(&tp,
                    &hdr_buf[BTS_SOMEIP_HEADER_SIZE],
                    BTS_SOMEIP_TP_HEADER_SIZE);
            if (ret < 0) return ret;

            /* Build iov: [header+tp] [payload slice...] — zero copy */
            bts_iov_t iov[SENDV_MAX_IOV];
            iov[0].base = hdr_buf;
            iov[0].len  = sizeof(hdr_buf);

            int slice_cnt = iov_slice(payload_iov, payload_iovcnt,
                                       offset, chunk_size,
                                       &iov[1], SENDV_MAX_IOV - 1);
            if (slice_cnt < 0) return BTS_ISO23150_ERR_INVALID_PARAM;

            ret = sendv_cb(iov, 1 + slice_cnt, user_data);
            if (ret < 0) return ret;

            offset += chunk_size;
        }
    }

    #undef SENDV_MAX_IOV

    (*session_id)++;

    return BTS_ISO23150_OK;
}
