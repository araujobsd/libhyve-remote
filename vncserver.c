/*
 * Copyright (c) 2017 by Marcelo Araujo <araujo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * the GNU Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <sysexits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "vncserver.h"
#include "libcheck.h"
#include "rfbsrv.h"
#include "hyverem.h"

static int hyverem_debug = 1;
#define DPRINTF(params) if (hyverem_debug) printf params
#define WPRINTF(params) printf params

static char *keys[0x400];

static int load_functions(void);
static enum rfbNewClientAction vncserver_newclient(rfbClientPtr cl);
static void dokey(rfbBool down, rfbKeySym k, rfbClientPtr cl);
static void doptr(int buttonMask, int x, int y, rfbClientPtr cl);

// Shared functions from libvncserver
rfbScreenInfoPtr (*get_screen)(int *argc, char **argv,
    int width, int height, int bitsPerSample, int samplesPerPixel,
    int bytesPerPixel);
void (*start_vnc_server)(rfbScreenInfoPtr rfbScreen);
void (*run_event_loop)(rfbScreenInfoPtr screeninfo, long usec,
    rfbBool runInBackground);
void (*mark_rect_asmodified)(rfbScreenInfoPtr rfbScreen, int x1, int y1,
    int x2, int y2);

static enum rfbNewClientAction
vncserver_newclient(rfbClientPtr cl) {
    char buffer[1024];
    struct sockaddr_in addr;
    socklen_t len=sizeof(addr);
    unsigned int ipv4;

    getpeername(cl->sock, (struct sockaddr *)&addr, &len);
    ipv4 = ntohl(addr.sin_addr.s_addr);
    printf("Client connected from ip %d.%d.%d.%d",
            (ipv4>>24)&0xff, (ipv4>>16)&0xff, (ipv4>>8)&0xff, ipv4&0xff);

    return RFB_CLIENT_ACCEPT;
}

static void
dokey(rfbBool down, rfbKeySym k, rfbClientPtr cl) {
    char buffer[1024 + 32];
    printf("%s: %s (0x%x)",
            down?"down":"up", keys[k&0x3ff]?keys[k&0x3ff]:"", (unsigned int)k);
}

static void
doptr(int buttonMask, int x, int y, rfbClientPtr cl) {
    char buffer[1024];
    if (buttonMask) {
        printf("Ptr: mouse button mask 0x%x at %d,%d", buttonMask,
                x, y);
    }
}


static int
load_functions(void) {
    char *loader = NULL;
    void *shlib;

    if (check_sharedlibs(LIBVNCSERVER) == 0) {
        loader = strdup(LIBVNCSERVER);
        if (loader == NULL)
            err(EX_SOFTWARE, "not possible to load %s", LIBVNCSERVER);

        shlib = dlopen(loader, RTLD_NOW | RTLD_GLOBAL);
        if (!shlib) {
            goto free_lib;
        }

        get_screen = dlsym(shlib, "rfbGetScreen");
        start_vnc_server = dlsym(shlib, "rfbInitServerWithPthreadsAndZRLE");
        run_event_loop = dlsym(shlib, "rfbRunEventLoop");
        mark_rect_asmodified = dlsym(shlib, "rfbMarkRectAsModified");

        if(!start_vnc_server) {
            WPRINTF(("Failed to load rfbInitServerWithPthreadsAndZRLE\n"));
            goto free_lib;
        } else if (!run_event_loop) {
            WPRINTF(("Failed to load rfbRunEventLoop\n"));
            goto free_lib;
        } else if (!mark_rect_asmodified) {
            WPRINTF(("Failed to load rfbMarkRectAsModified\n"));
            goto free_lib;
        }
        return (0);
    }
free_lib:
    dlclose(loader);
    free(loader);
    return (1);
}

int
init_server(struct server_softc *sc) {
    if (load_functions() == 0) {
        struct vncserver_handler *srv = malloc(sizeof(struct vncserver_handler));
        srv->vs_screen = (struct _rfbScreenInfo *)malloc(sizeof(rfbScreenInfoPtr));
        srv->vs_screen = (*get_screen)(NULL, NULL, sc->vs_width, sc->vs_height, 8, 3, 4);
        if (!srv->vs_screen) {
            WPRINTF(("Error to allocate vs_screen\n"));
            free(srv);
            return (1);
        }


        srv->vs_screen->desktopName = sc->desktopName;
        srv->vs_screen->alwaysShared = sc->alwaysShared;
        srv->vs_screen->serverFormat.redShift = sc->redShift;
        srv->vs_screen->serverFormat.greenShift = sc->greenShift;
        srv->vs_screen->serverFormat.blueShift = sc->blueShift;
        srv->vs_screen->frameBuffer = sc->frameBuffer;
        srv->vs_screen->screenData = sc;
        srv->vs_screen->port = sc->bind_port;
        srv->vs_screen->ipv6port = sc->bind_port;

        srv->vs_screen->ptrAddEvent = doptr;
        srv->vs_screen->kbdAddEvent = dokey;
        srv->vs_screen->newClientHook = vncserver_newclient;

        DPRINTF(("width: %d\t height: %d\n", sc->vs_width, sc->vs_height));
        DPRINTF(("Bind port: %d\n", sc->bind_port));
        DPRINTF(("Always share: %d\n", sc->alwaysShared));
        DPRINTF(("Desktop name: %s\n", sc->desktopName));

        start_vnc_server(srv->vs_screen);
        run_event_loop(srv->vs_screen, -1, FALSE);

        return (0);
    }
    return (1);
}

int
mark_rect_modified(struct server_softc *sc) {
    if (load_functions() == 0) {
        struct vncserver_handler *srv = malloc(sizeof(struct vncserver_handler));
        srv->vs_screen = (struct _rfbScreenInfo *)malloc(sizeof(rfbScreenInfoPtr));
        mark_rect_asmodified(srv->vs_screen, 0, 0, sc->vs_width,
            sc->vs_height);
    }

    return (0);
}
