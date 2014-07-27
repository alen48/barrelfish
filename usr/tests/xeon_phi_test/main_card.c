/*
 * Copyright (c) 2007-12 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */
#include <barrelfish/barrelfish.h>
#include <barrelfish/ump_chan.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <xeon_phi/xeon_phi_messaging.h>
#include <xeon_phi/xeon_phi_messaging_client.h>
#include <xeon_phi/xeon_phi_dma_client.h>

uint32_t send_reply = 0x0;

#include "benchmark.h"

uint8_t connected = 0;

static void *card_buf;
static struct capref card_frame;
static lpaddr_t card_base;
static size_t card_frame_sz;

static void *host_buf;
static struct capref host_frame;
static lpaddr_t host_base;
static size_t host_frame_sz;

static struct bench_bufs bufs;
static struct bench_bufs bufs_rev;

static struct ump_chan uc;
static struct ump_chan uc_rev;

static void *inbuf;
static void *outbuf;

static void *inbuf_rev;
static void *outbuf_rev;

static void init_buffer_c0(void)
{
#ifdef XPHI_BENCH_CHAN_HOST
    inbuf = host_buf + XPHI_BENCH_MSG_FRAME_SIZE;
    outbuf = host_buf;
    inbuf_rev = card_buf + XPHI_BENCH_MSG_FRAME_SIZE;;
    outbuf_rev = card_buf;
#endif

#ifdef XPHI_BENCH_CHAN_CARD
    inbuf = host_buf;
    outbuf = host_buf + XPHI_BENCH_MSG_FRAME_SIZE;
    inbuf_rev = card_buf;
    outbuf_rev = card_buf + XPHI_BENCH_MSG_FRAME_SIZE;
#endif

#ifdef XPHI_BENCH_CHAN_DEFAULT
    inbuf = host_buf;
    outbuf = card_buf;
    inbuf_rev = outbuf + XPHI_BENCH_MSG_FRAME_SIZE;
    outbuf_rev = inbuf + XPHI_BENCH_MSG_FRAME_SIZE;
#ifdef XPHI_BENCH_BUFFER_CARD
    bufs.buf = card_buf + 2 * XPHI_BENCH_MSG_FRAME_SIZE;
    bufs_rev.buf = host_buf + 2 * XPHI_BENCH_MSG_FRAME_SIZE;
#else
    bufs.buf = host_buf + 2 * XPHI_BENCH_MSG_FRAME_SIZE;
    bufs_rev.buf = card_buf + 2 * XPHI_BENCH_MSG_FRAME_SIZE;
#endif
#endif
}

static errval_t alloc_local(void)
{
    errval_t err;

    size_t frame_size = 0;

    frame_size = XPHI_BENCH_FRAME_SIZE_CARD;

    if (!frame_size) {
        frame_size = 4096;
    }

    debug_printf("Allocating a frame of size: %lx\n", frame_size);

    size_t alloced_size = 0;
    err = frame_alloc(&card_frame, frame_size, &alloced_size);
    assert(err_is_ok(err));
    assert(alloced_size >= frame_size);

    struct frame_identity id;
    err = invoke_frame_identify(card_frame, &id);
    assert(err_is_ok(err));
    card_base = id.base;
    card_frame_sz = alloced_size;

    err = vspace_map_one_frame(&card_buf, alloced_size, card_frame, NULL, NULL);

    return err;
}

static errval_t msg_open_cb(struct capref msgframe,
                            uint8_t chantype)
{
    errval_t err;

    struct frame_identity id;
    err = invoke_frame_identify(msgframe, &id);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "could not identify the frame");
    }

    debug_printf("msg_open_cb | Frame base: %016lx, size=%lx\n",
                 id.base,
                 1UL << id.bits);

    host_frame = msgframe;

    host_base = id.base;

    host_frame_sz = (1UL << id.bits);

    err = vspace_map_one_frame(&host_buf, 1UL << id.bits, msgframe, NULL, NULL);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "Could not map the frame");
    }

    init_buffer_c0();

    connected = 0x1;

    return SYS_ERR_OK;
}

static struct xeon_phi_messaging_cb callbacks = {
    .open = msg_open_cb
};

int main(int argc,
         char **argv)
{
    errval_t err;

    debug_printf("Xeon Phi Test started on the card.\n");

    debug_printf("Msg Buf Size = %lx, Buf Frame Size = %lx\n",
    XPHI_BENCH_MSG_FRAME_SIZE,
                 XPHI_BENCH_BUF_FRAME_SIZE);

    err = xeon_phi_messaging_service_init(&callbacks);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "could not init the service\n");
    }

    err = alloc_local();
    assert(err_is_ok(err));

    err = xeon_phi_messaging_service_start(XEON_PHI_MESSAGING_NO_HANDLER);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "could not start the service\n");
    }

    while (!connected) {
        messages_wait_and_handle_next();
    }

    char iface[30];
    snprintf(iface, 30, "xeon_phi_test.%u", XPHI_BENCH_CORE_HOST);

    debug_printf("sending open message to %s\n", iface);
    err = xeon_phi_messaging_open(0, iface, card_frame, XEON_PHI_CHAN_TYPE_UMP);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "could not open channel");
    }

    debug_printf("Initializing UMP channel...\n");

    err = ump_chan_init(&uc, inbuf,
                        XPHI_BENCH_MSG_FRAME_SIZE,
                        outbuf,
                        XPHI_BENCH_MSG_FRAME_SIZE);
    err = ump_chan_init(&uc_rev, inbuf_rev,
                        XPHI_BENCH_MSG_FRAME_SIZE,
                        outbuf_rev,
                        XPHI_BENCH_MSG_FRAME_SIZE);

#ifdef XPHI_BENCH_PROCESS_CARD
    delay_ms(1000);
#endif

#ifdef XPHI_BENCH_PROCESS_CARD
#ifndef XPHI_BENCH_THROUGHPUT
    debug_printf("---------------- normal run -----------------\n");
    xphi_bench_start_echo(&bufs, &uc);
    debug_printf("---------------- reversed run -----------------\n");
    xphi_bench_start_echo(&bufs_rev, &uc_rev);
#else
    debug_printf("---------------- normal run -----------------\n");
    xphi_bench_start_processor(&bufs, &uc);
    debug_printf("---------------- reversed run -----------------\n");
    xphi_bench_start_processor(&bufs_rev, &uc_rev);
#endif
#else
#ifndef XPHI_BENCH_THROUGHPUT
    debug_printf("---------------- normal run -----------------\n");
    xphi_bench_start_initator_rtt(&bufs, &uc);
    debug_printf("---------------- reversed run -----------------\n");
    xphi_bench_start_initator_rtt(&bufs_rev, &uc_rev);
#else
#ifdef XPHI_BENCH_SEND_SYNC
    debug_printf("---------------- normal run -----------------\n");
    xphi_bench_start_initator_sync(&bufs, &uc);
    debug_printf("---------------- reversed run -----------------\n");
    xphi_bench_start_initator_sync(&bufs-rev, &uc_rev);
#else
    debug_printf("---------------- normal run -----------------\n");
    xphi_bench_start_initator_async(&bufs, &uc);
    debug_printf("---------------- reversed run -----------------\n");
    xphi_bench_start_initator_async(&bufs_rev, &uc_rev);
#endif
#endif
#endif

#ifndef XPHI_BENCH_PROCESS_CARD
    debug_printf("+++++++ DMA / MEMCOPY Benchmark ++++++++\n");
    err = xeon_phi_dma_client_register(0, host_frame);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "could not register memory");
    }

    err = xeon_phi_dma_client_register(0, card_frame);
    if (err_is_fail(err)) {
        USER_PANIC_ERR(err, "could not register memory");
    }

    debug_printf("\n");
    debug_printf("========================================\n");
    debug_printf("\n");
    debug_printf("MEMCPY-BENCH: CARD LOCAL \n");
    debug_printf("\n");
    debug_printf("========================================\n");
    debug_printf("\n");

    xphi_bench_memcpy(card_buf + 2* XPHI_BENCH_MSG_FRAME_SIZE,
                    card_buf + 2* XPHI_BENCH_MSG_FRAME_SIZE
                    + (XPHI_BENCH_BUF_FRAME_SIZE / 2),
                    XPHI_BENCH_BUF_FRAME_SIZE / 2,
                    card_base + 2* XPHI_BENCH_MSG_FRAME_SIZE,
                    card_base + 2* XPHI_BENCH_MSG_FRAME_SIZE
                    + (XPHI_BENCH_BUF_FRAME_SIZE / 2));

    debug_printf("\n");
    debug_printf("========================================\n");
    debug_printf("\n");
    debug_printf("MEMCPY-BENCH: CARD -> HOST \n");
    debug_printf("\n");
    debug_printf("========================================\n");
    debug_printf("\n");
    xphi_bench_memcpy(host_buf + 2* XPHI_BENCH_MSG_FRAME_SIZE,
                    card_buf + 2* XPHI_BENCH_MSG_FRAME_SIZE,
                    XPHI_BENCH_BUF_FRAME_SIZE / 2,
                    host_base + 2* XPHI_BENCH_MSG_FRAME_SIZE,
                    card_base + 2* XPHI_BENCH_MSG_FRAME_SIZE);
    debug_printf("\n");

    debug_printf("========================================\n");
    debug_printf("\n");
    debug_printf("MEMCPY-BENCH: HOST -> CARD \n");
    debug_printf("\n");
    debug_printf("========================================\n");
    debug_printf("\n");

    xphi_bench_memcpy(card_buf + 2* XPHI_BENCH_MSG_FRAME_SIZE,
                    host_buf + 2* XPHI_BENCH_MSG_FRAME_SIZE,
                    XPHI_BENCH_BUF_FRAME_SIZE / 2,
                    card_base + 2* XPHI_BENCH_MSG_FRAME_SIZE,
                    host_base + 2* XPHI_BENCH_MSG_FRAME_SIZE);
#endif
}
