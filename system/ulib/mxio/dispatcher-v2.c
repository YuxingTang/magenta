// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <mxio/debug.h>
#include <mxio/dispatcher.h>
#include <system/listnode.h>

#include <runtime/thread.h>
#include <runtime/mutex.h>

#define MXDEBUG 0

typedef struct {
    list_node_t node;
    mx_handle_t h;
    uint32_t flags;
    void* cb;
    void* cookie;
} handler_t;

#define FLAG_DISCONNECTED 1

struct mxio_dispatcher {
    mxr_mutex_t lock;
    list_node_t list;
    mx_handle_t ioport;
    mxio_dispatcher_cb_t cb;
    mxr_thread_t* t;
};

static void mxio_dispatcher_destroy(mxio_dispatcher_t* md) {
    mx_handle_close(md->ioport);
    free(md);
}

static void destroy_handler(mxio_dispatcher_t* md, handler_t* handler) {
    mxr_mutex_lock(&md->lock);
    list_delete(&handler->node);
    mxr_mutex_unlock(&md->lock);
    mx_handle_close(handler->h);
    free(handler);
}

static void disconnect_handler(mxio_dispatcher_t* md, handler_t* handler) {
    // unbind, so we get no further messages
    mx_io_port_bind(md->ioport, (uint64_t)(uintptr_t)handler, handler->h, 0);

    // send a synthetic message so we know when it's safe to destroy
    mx_io_packet_t packet;
    packet.hdr.key = (uint64_t)(uintptr_t)handler;
    packet.signals = MX_SIGNAL_SIGNALED;
    mx_io_port_queue(md->ioport, &packet, sizeof(packet));

    // flag so we know to ignore further events
    handler->flags |= FLAG_DISCONNECTED;
}

static int mxio_dispatcher_thread(void* _md) {
    mxio_dispatcher_t* md = _md;
    mx_status_t r;

again:
    for (;;) {
        mx_io_packet_t packet;
        if ((r = mx_io_port_wait(md->ioport, &packet, sizeof(packet))) < 0) {
            printf("dispatcher: ioport wait failed %d\n", r);
            break;
        }
        handler_t* handler = (void*)(uintptr_t)packet.hdr.key;
        if (handler->flags & FLAG_DISCONNECTED) {
            // handler is awaiting gc
            // ignore events for it until we get the synthetic "destroy" event
            if (packet.signals & MX_SIGNAL_SIGNALED) {
                destroy_handler(md, handler);
            }
            continue;
        }
        if (packet.signals & MX_SIGNAL_READABLE) {
            // for now we must drain all readable messages
            // due to limitations of io ports
            for (;;) {
                if ((r = md->cb(handler->h, handler->cb, handler->cookie)) != 0) {
                    if (r == ERR_DISPATCHER_NO_WORK) {
                        // no more messages to read
                        break;
                    }
                    if (r < 0) {
                        // synthesize a close
                        md->cb(0, handler->cb, handler->cookie);
                    }
                    disconnect_handler(md, handler);
                    goto again;
                }
            }
        }
        if (packet.signals & MX_SIGNAL_PEER_CLOSED) {
            // synthesize a close
            md->cb(0, handler->cb, handler->cookie);
            disconnect_handler(md, handler);
        }
    }

    printf("dispatcher: FATAL ERROR, EXITING\n");
    mxio_dispatcher_destroy(md);
    return NO_ERROR;
}

mx_status_t mxio_dispatcher_create(mxio_dispatcher_t** out, mxio_dispatcher_cb_t cb) {
    mxio_dispatcher_t* md;
    if ((md = calloc(1, sizeof(*md))) == NULL) {
        return ERR_NO_MEMORY;
    }
    xprintf("mxio_dispatcher_create: %p\n", md);
    list_initialize(&md->list);
    md->lock = MXR_MUTEX_INIT;
    if ((md->ioport = mx_io_port_create(0u)) < 0) {
        free(md);
        return md->ioport;
    }
    md->cb = cb;
    *out = md;
    return NO_ERROR;
}

mx_status_t mxio_dispatcher_start(mxio_dispatcher_t* md) {
    mx_status_t r;
    mxr_mutex_lock(&md->lock);
    if (md->t == NULL) {
        if (mxr_thread_create(mxio_dispatcher_thread, md, "mxio-dispatcher", &md->t)) {
            mxio_dispatcher_destroy(md);
            r = ERR_NO_RESOURCES;
        } else {
            mxr_thread_detach(md->t);
            r = NO_ERROR;
        }
    } else {
        r = ERR_BAD_STATE;
    }
    mxr_mutex_unlock(&md->lock);
    return r;
}

void mxio_dispatcher_run(mxio_dispatcher_t* md) {
    mxio_dispatcher_thread(md);
}

mx_status_t mxio_dispatcher_add(mxio_dispatcher_t* md, mx_handle_t h, void* cb, void* cookie) {
    handler_t* handler;
    mx_status_t r;

    if ((handler = malloc(sizeof(handler_t))) == NULL) {
        return ERR_NO_MEMORY;
    }
    handler->h = h;
    handler->flags = 0;
    handler->cb = cb;
    handler->cookie = cookie;

    mxr_mutex_lock(&md->lock);
    list_add_tail(&md->list, &handler->node);
    if ((r = mx_io_port_bind(md->ioport, (uint64_t)(uintptr_t)handler, h,
                             MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED)) < 0) {
        list_delete(&handler->node);
    }
    mxr_mutex_unlock(&md->lock);

    if (r < 0) {
        printf("dispatcher: failed to bind: %d\n", r);
        free(handler);
    }
    return r;
}
