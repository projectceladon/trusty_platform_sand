/*******************************************************************************
 * Copyright (c) 2018 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <uapi/err.h>

#include <trusty_std.h>
#include "trusty_key_migration.h"

#define LOG_TAG "libhwkey_migration"
#define TLOGE(fmt, ...) \
    fprintf(stderr, "%s: %d: " fmt, LOG_TAG, __LINE__,  ## __VA_ARGS__)

static void * (* const volatile memset_ptr)(void *, int, size_t) = memset;

void secure_memzero(void * p, size_t len)
{
    (memset_ptr)(p, 0, len);
}

/**
 * long hwkey_err_to_tipc_err() - translates hwkey err value to tipc/lk err value
 * @hwkey_err: hwkey err value
 *
 * Returns: enum hwkey_err value
 */
static long hwkey_err_to_tipc_err(enum hwkey_err hwkey_err)
{
    switch(hwkey_err) {
        case HWKEY_NO_ERROR:
            return NO_ERROR;
        case HWKEY_ERR_NOT_VALID:
            return ERR_NOT_VALID;
        case HWKEY_ERR_BAD_LEN:
            return ERR_BAD_LEN;
        case HWKEY_ERR_NOT_IMPLEMENTED:
            return ERR_NOT_IMPLEMENTED;
        case HWKEY_ERR_NOT_FOUND:
            return ERR_NOT_FOUND;
        default:
            return ERR_GENERIC;
    }
}

/**
 * long send_req() - sends request to hwkey server
 * @session: the hwkey session handle
 * @msg: the request header to send to the hwkey server
 * @req_buf: the request payload to send to the hwkey server
 * @req_buf_len: the length of the request payload @req_buf
 * @rsp_buf: buffer in which to store the response payload
 * @rsp_buf_len: the size of the response buffer. Inout param, set
 *               to the actual response payload length.
 *
 * Returns: NO_ERROR on success, negative error code on failure
 */
static long send_req(hwkey_session_t session, struct hwkey_msg *msg, uint8_t *req_buf,
        uint32_t req_buf_len, uint8_t *rsp_buf, uint32_t *rsp_buf_len)
{
    long rc;

    iovec_t tx_iov[2] = {
        {
            .base = msg,
            .len = sizeof(*msg),
        },
        {
            .base = req_buf,
            .len = req_buf_len,
        },
    };
    ipc_msg_t tx_msg = {
        .iov = tx_iov,
        .num_iov = 2,
    };

    rc = send_msg(session, &tx_msg);
    if (rc < 0) {
        goto err_send_fail;
    }

    if(((size_t) rc) != sizeof(*msg) + req_buf_len) {
        rc = ERR_IO;
        goto err_send_fail;
    }

    uevent_t uevt;
    rc = wait(session, &uevt, -1);
    if (rc != NO_ERROR) {
        goto err_send_fail;
    }

    ipc_msg_info_t inf;
    rc = get_msg(session, &inf);
    if (rc != NO_ERROR) {
        TLOGE("%s: failed to get_msg (%ld)\n", __func__, rc);
        goto err_send_fail;
    }

    if (inf.len > sizeof(*msg) + *rsp_buf_len) {
        TLOGE("%s: insufficient output buffer size (%zu > %zu)\n",
                __func__, inf.len - sizeof(*msg), (size_t)*rsp_buf_len);
        rc = ERR_TOO_BIG;
        goto err_get_fail;
    }

    if (inf.len < sizeof(*msg)) {
        TLOGE("%s: short buffer (%u)\n", __func__, inf.len);
        rc = ERR_NOT_VALID;
        goto err_get_fail;
    }

    uint32_t cmd_sent = msg->cmd;

    iovec_t rx_iov[2] = {
        {
            .base = msg,
            .len = sizeof(*msg)
        },
        {
            .base = rsp_buf,
            .len = *rsp_buf_len
        }
    };
    ipc_msg_t rx_msg = {
        .iov = rx_iov,
        .num_iov = 2,
    };

    rc = read_msg(session, inf.id, 0, &rx_msg);
    put_msg(session, inf.id);
    if (rc < 0) {
        goto err_read_fail;
    }

    uint32_t read_len = rc;
    if (read_len != inf.len) {
        // data read in does not match message length
        TLOGE("%s: invalid read length (%u != %u)\n",
                __func__, read_len, inf.len);
        rc = ERR_IO;
        goto err_read_fail;
    }

    if (msg->cmd != (cmd_sent | HWKEY_RESP_BIT)) {
        TLOGE("%s: invalid response id (0x%x) for cmd (0x%x)\n",
                __func__, msg->cmd, cmd_sent);
        return ERR_NOT_VALID;
    }

    *rsp_buf_len = read_len - sizeof(*msg);
    return hwkey_err_to_tipc_err(msg->status);

err_get_fail:
    put_msg(session, inf.id);
err_send_fail:
err_read_fail:
    TLOGE("%s: failed read_msg (%ld)", __func__, rc);
    return rc;
}

long hwkey_generate_crypto_context(hwkey_session_t session, uint8_t *data, uint32_t data_size)
{
    if (data_size == 0 || data == NULL) {
        TLOGE("invalid args!\n");
        return ERR_NOT_VALID;
    }

    struct hwkey_msg msg = {
        .cmd = HWKEY_GENERATE_CRYPTO_CTX,
        .arg1 = data_size,
    };

    uint32_t stored_data_size = data_size;
    long rc = send_req(session, &msg, data, data_size, data, &data_size);
    if (rc != NO_ERROR) {
        TLOGE("send_req HWKEY_GENERATE_CRYPTO_CTX failed!\n");
        return rc;
    }

    if (stored_data_size != data_size) {
        TLOGE("data_size error!\n");
        return ERR_BAD_LEN;
    }

    return rc;
}

long hwkey_exchange_crypto_context(hwkey_session_t session, const uint8_t *src,
        uint8_t *dst, uint32_t dst_size)
{
    if (dst_size == 0 || dst == NULL || src == NULL) {
        TLOGE("invalid args!\n");
        return ERR_NOT_VALID;
    }

    struct hwkey_msg msg = {
        .cmd = HWKEY_EXCHANGE_CRYPTO_CTX,
        .arg1 = dst_size,
    };

    uint32_t stored_dst_size = dst_size;
    long rc = send_req(session, &msg, (uint8_t *) src, dst_size, dst, &dst_size);
    if (rc != NO_ERROR) {
        TLOGE("send_req HWKEY_EXCHANGE_CRYPTO_CTX failed!\n");
        return rc;
    }

    if (stored_dst_size != dst_size) {
        TLOGE("data_size error!\n");
        return ERR_BAD_LEN;
    }

    return rc;
}

long hwkey_get_ssek(hwkey_session_t session, uint8_t *ssek, uint32_t ssek_size)
{
    if (ssek_size == 0 || ssek == NULL) {
        TLOGE("invalid args!\n");
        return ERR_NOT_VALID;
    }

    struct hwkey_msg msg = {
        .cmd = HWKEY_GET_SSEK,
        .arg1 = ssek_size,
    };

    uint32_t stored_ssek_size = ssek_size;
    long rc = send_req(session, &msg, ssek, ssek_size, ssek, &ssek_size);
    if (rc != NO_ERROR) {
        TLOGE("send_req HWKEY_GET_SSEK failed!\n");
        return rc;
    }

    if (stored_ssek_size != ssek_size) {
        TLOGE("data_size error!\n");
        return ERR_BAD_LEN;
    }

    return rc;
}
