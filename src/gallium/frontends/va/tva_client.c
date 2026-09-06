/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tva_client.c - termux-va daemon client library implementation.
 *
 * Copyright (C) 2026 lfdevs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * ******************************************************************************
 * MODIFICATION NOTICE (GPL-3.0 section 5)
 *
 * This file is a MODIFIED version of vaapi-driver/src/dmd_client.c from the
 * droidspaces-media-decode project (Apache License, Version 2.0), ported to
 * the termux-va project and relicensed under GPL-3.0.  Modifications:
 *   - the TCP transport was removed entirely: the control channel is always
 *     a path-based Unix socket, and the "TCP" framing mode is called inline
 *     (wire value 0 unchanged),
 *   - protocol constants come from tva_protocol.h (mirror of the daemon's
 *     common/tva_protocol.h; values unchanged),
 *   - symbols renamed dmd_* -> tva_*; logs translated to English,
 *   - endpoint defaults resolve in container view via tva_default_endpoint()
 *     (TERMUX_VA_SOCKET > TERMUX_VA_SOCKET_DIR > shared-tmp paths) using
 *     Mesa's os_get_option (which also consults Android system properties),
 *   - the SHM slot release deadline comment updated for the daemon's 15s
 *     slot wait.
 * Everything else - the v3 handshake with the endpoint inode reconciliation,
 * the v2 downgrade retry, the SHM attach via abstract socket + SCM_RIGHTS,
 * the format-block handling - is kept faithful to the original.
 * ******************************************************************************
 *
 * Implementation decisions inherited from the upstream library:
 *
 * 1) All fds non-blocking + poll: the host is a browser; unbounded blocking
 *    anywhere would hang the whole process.  connect() also goes through
 *    non-blocking + POLLOUT + SO_ERROR.
 * 2) recv/send always loop to completion, handling EINTR and short
 *    transfers; sends carry MSG_NOSIGNAL.
 * 3) SHM is only an INTENT: the daemon sends the handshake response BEFORE
 *    the memfd handoff and silently downgrades to inline framing if the
 *    handoff times out.  A failed pickup is therefore not an error - just
 *    continue with inline framing.  The fallback is mandatory here.
 * 4) Error propagation: every failure path records code + human-readable
 *    reason (including strerror); callers read tva_session_last_error().
 * 5) No global mutable state: even the log flag lives per session.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE     /* MSG_CMSG_CLOEXEC / SOCK_CLOEXEC */
#endif
#include "tva_client.h"

#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "util/os_misc.h"

struct tva_session {
    int fd;                 /* daemon connection, non-blocking */
    int log;                /* log flag, from DMD_VA_LOG */

    int codec;
    int io_timeout_ms;

    char ep_path[108];      /* path used for connect(); stat'ed after the
                             * handshake for the dev/ino reconciliation */

    int xfer;               /* effective transport (TVA_XFER_*) */
    int eos;                /* peer close observed */
    int tx_broken;          /* uplink corrupted (send timed out mid-unit) */
    volatile int cancelled; /* abortive cancel requested by the owner */

    struct tva_format fmt;
    struct tva_error  err;

    /* Inline-mode receive buffer (reused, grown on demand) */
    uint8_t *rbuf;
    size_t   rbuf_size;
    int      rbuf_busy;     /* 1 = a frame is outstanding against rbuf */

    /* SHM pool */
    uint8_t *shm_base;
    size_t   shm_slot_bytes;
    size_t   shm_total;
    int      shm_slots;
    int      shm_held;      /* outstanding slot count, diagnostics */

    uint64_t units_sent;
    uint64_t frames_recv;
    uint32_t caps;          /* daemon capability bits from the format block */
    uint32_t last_pts;      /* unit index of the latest frame, 0 = none */
};

/* ------------------------------------------------------------ logging */
/*
 * Silent by default; DMD_VA_LOG=1 enables it.  stderr only.
 */
static void tva_c_log(const struct tva_session *s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void tva_c_log(const struct tva_session *s, const char *fmt, ...)
{
    if (!s || !s->log)
        return;
    va_list ap;
    va_start(ap, fmt);
    fputs("[tva-client] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int tva_log_wanted(void)
{
    const char *env = getenv("DMD_VA_LOG");
    return (env && env[0] == '1') ? 1 : 0;
}

/* ------------------------------------------------------------ errors */
static void err_set(struct tva_error *e, int code, int hs_status,
                    const char *what, int use_errno)
{
    if (!e)
        return;
    e->code = code;
    e->handshake_status = hs_status;
    if (use_errno) {
        int sv = errno;
        snprintf(e->msg, sizeof(e->msg), "%s: %s", what, strerror(sv));
        errno = sv;
    } else {
        snprintf(e->msg, sizeof(e->msg), "%s", what);
    }
}

static int sess_err(struct tva_session *s, int code, const char *what,
                    int use_errno)
{
    err_set(&s->err, code, 0, what, use_errno);
    if (s->log)
        tva_c_log(s, "error(%d): %s", code, s->err.msg);
    return code;
}

/* ------------------------------------------------------------ fd helpers */
/* All sockets are created with SOCK_CLOEXEC | SOCK_NONBLOCK in one call so
 * no fcntl window exists (a fork race would leak fds into host children). */

/* Wait until fd is readable/writable.  Returns 1 ready, 0 timeout, -1 error
 * (errno valid).  timeout_ms < 0 is treated as 0 - the library never waits
 * without a bound. */
static int wait_fd(int fd, short events, int timeout_ms)
{
    if (timeout_ms < 0)
        timeout_ms = 0;
    struct pollfd p;
    p.fd = fd;
    p.events = events;
    for (;;) {
        p.revents = 0;
        int r = poll(&p, 1, timeout_ms);
        if (r < 0) {
            if (errno == EINTR)
                continue;   /* retry with the original timeout on EINTR */
            return -1;
        }
        if (r == 0)
            return 0;
        return 1;
    }
}

/*
 * Receive exactly len bytes.
 *   first_timeout_ms: bound for the FIRST byte (can be short, "any frame?")
 *   rest_timeout_ms:  bound per step after the first byte (must not be
 *                     short, or frames get truncated)
 * Returns TVA_OK / TVA_EOS (peer closed before any byte) / TVA_ERR_*.
 * A peer close mid-message is TVA_ERR_PROTOCOL: a truncated message is not
 * a clean end of stream.
 */
static int recv_exact(struct tva_session *s, void *buf, size_t len,
                      int first_timeout_ms, int rest_timeout_ms)
{
    uint8_t *p = buf;
    size_t got = 0;

    while (got < len) {
        if (__atomic_load_n(&s->cancelled, __ATOMIC_ACQUIRE))
            return TVA_ERR_CANCELLED;
        int to = (got == 0) ? first_timeout_ms : rest_timeout_ms;
        int r = wait_fd(s->fd, POLLIN, to);
        if (r < 0)
            return sess_err(s, TVA_ERR_IO, "poll for readability failed", 1);
        if (__atomic_load_n(&s->cancelled, __ATOMIC_ACQUIRE))
            return TVA_ERR_CANCELLED;
        if (r == 0) {
            if (got == 0)
                return TVA_ERR_TIMEOUT;   /* clean "nothing yet" */
            return sess_err(s, TVA_ERR_TIMEOUT, "receive timed out mid-message", 0);
        }

        ssize_t n = recv(s->fd, p + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return sess_err(s, TVA_ERR_IO, "recv failed", 1);
        }
        if (n == 0) {
            if (got == 0) {
                s->eos = 1;
                return TVA_EOS;
            }
            return sess_err(s, TVA_ERR_PROTOCOL,
                            "peer closed mid-message (message truncated)", 0);
        }
        got += (size_t)n;
    }
    return TVA_OK;
}

/* Send exactly len bytes; MSG_NOSIGNAL keeps a closed peer from SIGPIPE-ing
 * the host process. */
static int send_exact(struct tva_session *s, const void *buf, size_t len,
                      int timeout_ms)
{
    const uint8_t *p = buf;
    size_t sent = 0;

    while (sent < len) {
        if (__atomic_load_n(&s->cancelled, __ATOMIC_ACQUIRE))
            return TVA_ERR_CANCELLED;
        ssize_t n = send(s->fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                int r = wait_fd(s->fd, POLLOUT, timeout_ms);
                if (__atomic_load_n(&s->cancelled, __ATOMIC_ACQUIRE))
                    return TVA_ERR_CANCELLED;
                if (r < 0)
                    return sess_err(s, TVA_ERR_IO, "poll for writability failed", 1);
                if (r == 0)
                    return sess_err(s, TVA_ERR_TIMEOUT, "send timed out", 0);
                continue;
            }
            if (errno == EPIPE || errno == ECONNRESET)
                return sess_err(s, TVA_ERR_IO, "connection closed by the daemon", 1);
            return sess_err(s, TVA_ERR_IO, "send failed", 1);
        }
        sent += (size_t)n;
    }
    return TVA_OK;
}

/* ------------------------------------------------------------ shared memory */
static volatile uint32_t *shm_state_word(struct tva_session *s, int idx)
{
    return (volatile uint32_t *)(s->shm_base + (size_t)idx * sizeof(uint32_t));
}

/*
 * Connect to the daemon's abstract socket and take the memfd via
 * SCM_RIGHTS.  Returns -1 on failure (caller downgrades to inline framing;
 * not a fatal error).
 *
 * Abstract address layout: sun_path[0] = 0, name from sun_path+1,
 * addrlen = offsetof(sun_path) + 1 + strlen(name) - exactly symmetric to
 * the daemon's bind().
 */
static int shm_attach(struct tva_session *s, const char *name)
{
    int sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (sock < 0) {
        tva_c_log(s, "abstract socket creation failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un ua;
    memset(&ua, 0, sizeof(ua));
    ua.sun_family = AF_UNIX;
    size_t nl = strlen(name);
    if (nl > sizeof(ua.sun_path) - 2) {
        tva_c_log(s, "shared memory name too long: %zu", nl);
        close(sock);
        return -1;
    }
    ua.sun_path[0] = 0;
    memcpy(ua.sun_path + 1, name, nl);
    socklen_t ulen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + nl);

    if (connect(sock, (struct sockaddr *)&ua, ulen) < 0) {
        if (errno != EINPROGRESS) {
            tva_c_log(s, "connect @%s failed: %s", name, strerror(errno));
            close(sock);
            return -1;
        }
        if (wait_fd(sock, POLLOUT, TVA_SHM_ATTACH_MS) != 1) {
            tva_c_log(s, "connect @%s timed out", name);
            close(sock);
            return -1;
        }
        int se = 0;
        socklen_t sl = sizeof(se);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &se, &sl) < 0 || se != 0) {
            tva_c_log(s, "connect @%s failed: %s", name, strerror(se ? se : errno));
            close(sock);
            return -1;
        }
    }

    /* 12 bytes [slots][slot bytes][pool total] plus the memfd in SCM_RIGHTS.
     * The daemon sends it in one sendmsg; ancillary data never splits across
     * messages, but recvmsg may still EAGAIN first on a non-blocking fd. */
    uint32_t meta[3];
    struct iovec io;
    io.iov_base = meta;
    io.iov_len = sizeof(meta);
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr mh;
    ssize_t n;

    for (;;) {
        if (wait_fd(sock, POLLIN, TVA_SHM_ATTACH_MS) != 1) {
            tva_c_log(s, "timed out waiting for the memfd");
            close(sock);
            return -1;
        }
        memset(cbuf, 0, sizeof(cbuf));
        memset(&mh, 0, sizeof(mh));
        mh.msg_iov = &io;
        mh.msg_iovlen = 1;
        mh.msg_control = cbuf;
        mh.msg_controllen = sizeof(cbuf);
        n = recvmsg(sock, &mh, MSG_CMSG_CLOEXEC);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        break;
    }

    if (n != (ssize_t)sizeof(meta)) {
        tva_c_log(s, "failed to receive shared memory parameters: n=%zd (%s)", n,
                  n < 0 ? strerror(errno) : "length mismatch");
        close(sock);
        return -1;
    }

    int mfd = -1;
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&mh); cm; cm = CMSG_NXTHDR(&mh, cm)) {
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_RIGHTS &&
            cm->cmsg_len == CMSG_LEN(sizeof(int))) {
            memcpy(&mfd, CMSG_DATA(cm), sizeof(int));
            break;
        }
    }
    close(sock);
    if (mfd < 0) {
        tva_c_log(s, "no memfd in the response");
        return -1;
    }

    int slots      = (int)ntohl(meta[0]);
    size_t slot_sz = (size_t)ntohl(meta[1]);
    size_t total   = (size_t)ntohl(meta[2]);

    /* Self-check: refuse to read shared memory through inconsistent layout */
    if (slots <= 0 || slots > 64 || slot_sz == 0 ||
        total < SHM_CTRL_BYTES + slot_sz * (size_t)slots ||
        (size_t)slots * sizeof(uint32_t) > SHM_CTRL_BYTES) {
        tva_c_log(s, "inconsistent shared memory parameters: slots=%d slot=%zu total=%zu",
                  slots, slot_sz, total);
        close(mfd);
        return -1;
    }

    void *base = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
    close(mfd);                     /* fd no longer needed after mmap */
    if (base == MAP_FAILED) {
        tva_c_log(s, "mmap of shared memory failed: %s", strerror(errno));
        return -1;
    }

    s->shm_base       = base;
    s->shm_slots      = slots;
    s->shm_slot_bytes = slot_sz;
    s->shm_total      = total;
    tva_c_log(s, "shared memory attached: %d slots x %zu bytes (total %zu)",
              slots, slot_sz, total);
    return 0;
}

/* ------------------------------------------------- connection & handshake */
/* Non-blocking connect with a bounded wait. */
static int sock_connect_wait(int fd, const struct sockaddr *sa, socklen_t slen,
                             const char *what, int timeout_ms,
                             struct tva_error *err)
{
    if (connect(fd, sa, slen) < 0) {
        if (errno != EINPROGRESS) {
            char m[96];
            snprintf(m, sizeof(m), "connect %s failed", what);
            err_set(err, TVA_ERR_CONNECT, 0, m, 1);
            close(fd);
            return -1;
        }
        int r = wait_fd(fd, POLLOUT, timeout_ms);
        if (r < 0) {
            err_set(err, TVA_ERR_CONNECT, 0, "poll during connect failed", 1);
            close(fd);
            return -1;
        }
        if (r == 0) {
            err_set(err, TVA_ERR_CONNECT, 0, "connect timed out", 0);
            close(fd);
            return -1;
        }
        int se = 0;
        socklen_t sl = sizeof(se);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &se, &sl) < 0) {
            err_set(err, TVA_ERR_CONNECT, 0, "getsockopt(SO_ERROR) failed", 1);
            close(fd);
            return -1;
        }
        if (se != 0) {
            errno = se;
            err_set(err, TVA_ERR_CONNECT, 0, "connect refused", 1);
            close(fd);
            return -1;
        }
    }
    return 0;
}

/* Connect to a path-based Unix socket.
 *
 * The socket lives in the shared tmp directory, so the container and Termux
 * see the same file; authentication is plain file permissions.  The receive
 * buffer must be enlarged explicitly (see the 4MB note) - the AF_UNIX
 * default of 224KB cannot hold a single NV12 frame, which once collapsed
 * throughput below realtime. */
static int unix_connect(struct tva_session *s, const char *path,
                        int timeout_ms, struct tva_error *err)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        err_set(err, TVA_ERR_CONNECT, 0, "failed to create Unix socket", 1);
        return -1;
    }

    struct sockaddr_un ua;
    memset(&ua, 0, sizeof(ua));
    ua.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(ua.sun_path)) {
        err_set(err, TVA_ERR_CONNECT, 0, "socket path too long", 0);
        close(fd);
        return -1;
    }
    memcpy(ua.sun_path, path, strlen(path));

    if (sock_connect_wait(fd, (struct sockaddr *)&ua, sizeof(ua),
                          path, timeout_ms, err) < 0)
        return -1;

    {
        int bufsz = 4 * 1024 * 1024;
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufsz, sizeof(bufsz));
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufsz, sizeof(bufsz));
    }

    /* Remember the endpoint path for the post-handshake inode check */
    snprintf(s->ep_path, sizeof(s->ep_path), "%s", path);
    s->fd = fd;
    return 0;
}

/*
 * Handshake.  The request is exactly 24 bytes, all big-endian:
 *   [4B magic][4B version][4B codec][4B width][4B height][4B xfer]
 * The daemon reads 4 bytes of magic first, then the remaining 20 - the
 * request must be written in one piece.  This client sends version 3.
 *
 * The response is at least 12 bytes: [status][actual xfer][namelen].  With
 * v3 and status==0, bit31 of namelen marks 16 extra bytes of endpoint
 * extension (dev/ino split into high/low u32s); the client stats the path
 * it connected to and reconciles, failing with TVA_ERR_ENDPOINT_MISMATCH on
 * a mismatch.  Old daemons reply with a bare 12 bytes -> check skipped.
 */
static int do_handshake(struct tva_session *s,
                        const struct tva_session_config *cfg,
                        uint32_t use_version,
                        struct tva_error *err)
{
    uint32_t hello[6];
    hello[0] = htonl(HELLO_MAGIC);
    hello[1] = htonl(use_version);
    hello[2] = htonl((uint32_t)cfg->codec);
    hello[3] = htonl((uint32_t)cfg->width);
    hello[4] = htonl((uint32_t)cfg->height);
    hello[5] = htonl(cfg->want_shm ? (uint32_t)XFER_SHM
                                   : (uint32_t)XFER_INLINE);

    if (send_exact(s, hello, sizeof(hello), s->io_timeout_ms) != TVA_OK) {
        if (err)
            *err = s->err;
        return -1;
    }

    uint32_t head[3];
    int r = recv_exact(s, head, sizeof(head), s->io_timeout_ms, s->io_timeout_ms);
    if (r != TVA_OK) {
        if (r == TVA_EOS)
            sess_err(s, TVA_ERR_PROTOCOL,
                     "daemon closed without a handshake response (version mismatch?)", 0);
        else if (r == TVA_ERR_TIMEOUT)
            sess_err(s, TVA_ERR_TIMEOUT, "timed out waiting for the handshake response", 0);
        if (err)
            *err = s->err;
        return -1;
    }

    uint32_t status = ntohl(head[0]);
    uint32_t mode   = ntohl(head[1]);
    uint32_t nlen_w = ntohl(head[2]);
    int      has_ext = (nlen_w >> 31) != 0;   /* v3: bit31 = endpoint extension */
    uint32_t nlen   = nlen_w & 0x7fffffffu;
    uint64_t ep_dev = 0, ep_ino = 0;

    /* Error paths reply with a bare 12 bytes (nlen=0); handle rejection
     * before reading anything else */
    if (status != 0) {
        const char *why = (status == 1) ? "daemon rejected handshake: protocol version not supported"
                        : (status == 2) ? "daemon rejected handshake: codec not supported"
                        : (status == 3) ? "daemon rejected handshake: resolution outside 96x96..8192x4320"
                        : (status == 4) ? "daemon rejected handshake: handshake missing"
                        : "daemon rejected handshake: unknown status";
        err_set(&s->err, TVA_ERR_REJECTED, (int)status, why, 0);
        if (err)
            *err = s->err;
        return -1;
    }

    /* v3 endpoint extension: [u32 dev_hi][u32 dev_lo][u32 ino_hi][u32 ino_lo] */
    if (has_ext) {
        uint32_t ext[4];
        if (recv_exact(s, ext, sizeof(ext),
                       s->io_timeout_ms, s->io_timeout_ms) != TVA_OK) {
            sess_err(s, TVA_ERR_PROTOCOL, "failed to read the endpoint extension", 0);
            if (err)
                *err = s->err;
            return -1;
        }
        ep_dev = ((uint64_t)ntohl(ext[0]) << 32) | ntohl(ext[1]);
        ep_ino = ((uint64_t)ntohl(ext[2]) << 32) | ntohl(ext[3]);
    }

    char name[64];
    memset(name, 0, sizeof(name));
    if (nlen > 0) {
        if (nlen >= sizeof(name)) {
            sess_err(s, TVA_ERR_PROTOCOL, "illegal name length in the handshake response", 0);
            if (err)
                *err = s->err;
            return -1;
        }
        if (recv_exact(s, name, nlen, s->io_timeout_ms, s->io_timeout_ms) != TVA_OK) {
            sess_err(s, TVA_ERR_PROTOCOL, "failed to read the shared memory name", 0);
            if (err)
                *err = s->err;
            return -1;
        }
    }

    /*
     * Endpoint inode reconciliation - the core of the v3 extension.
     *
     * connect() succeeding only means "something is listening on that
     * path", not that it is the endpoint we mean.  If a single socket FILE
     * was bind-mounted (instead of a directory) and the daemon restarted,
     * the client resolves a stale orphan socket; both sides may even stat
     * the SAME orphan inode.  The daemon now reports its listening
     * endpoint's (st_dev, st_ino) and the client verifies - on mismatch,
     * fail immediately with an actionable message instead of running on a
     * fake connection.
     */
    if (s->ep_path[0] && !(ep_dev == 0 && ep_ino == 0)) {
        struct stat st;
        if (stat(s->ep_path, &st) != 0) {
            char msg[192];
            snprintf(msg, sizeof(msg),
                     "cannot stat endpoint %s for the inode check, refusing to continue", s->ep_path);
            sess_err(s, TVA_ERR_ENDPOINT_MISMATCH, msg, 1);
            if (err)
                *err = s->err;
            return -1;
        }
        uint64_t my_dev = (uint64_t)st.st_dev, my_ino = (uint64_t)st.st_ino;
        if (my_dev != ep_dev || my_ino != ep_ino) {
            if (s->log)
                tva_c_log(s, "endpoint inode mismatch details: path=%s "
                             "stat(dev=%llu,ino=%llu) != daemon(dev=%llu,ino=%llu)",
                          s->ep_path,
                          (unsigned long long)my_dev, (unsigned long long)my_ino,
                          (unsigned long long)ep_dev, (unsigned long long)ep_ino);
            char msg[192];
            snprintf(msg, sizeof(msg),
                     "endpoint inode mismatch: stat(ino=%llu) != daemon(ino=%llu)"
                     "; the mount points at a stale socket (bind-mount the directory,"
                     " not the socket file)",
                     (unsigned long long)my_ino, (unsigned long long)ep_ino);
            sess_err(s, TVA_ERR_ENDPOINT_MISMATCH, msg, 0);
            if (err)
                *err = s->err;
            return -1;
        }
        if (s->log)
            tva_c_log(s, "endpoint check passed: dev=%llu ino=%llu",
                      (unsigned long long)my_dev, (unsigned long long)my_ino);
    }

    /* mode==SHM is only the daemon's intent: the response precedes the memfd
     * handoff, and a failed handoff downgrades silently.  A failed pickup is
     * therefore never fatal - continue with inline framing. */
    s->xfer = XFER_INLINE;
    if (mode == (uint32_t)XFER_SHM && nlen > 0) {
        if (shm_attach(s, name) == 0)
            s->xfer = XFER_SHM;
        else
            tva_c_log(s, "shared memory pickup failed, continuing with inline framing");
    }
    return 0;
}

/* ------------------------------------------------------------ public API */
void tva_session_config_defaults(struct tva_session_config *cfg)
{
    if (!cfg)
        return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->codec = CODEC_H264;
    cfg->connect_timeout_ms = TVA_DEF_CONNECT_MS;
    cfg->io_timeout_ms = TVA_DEF_IO_MS;
}

const char *tva_default_endpoint(char *buf, size_t bufsz)
{
    const char *env_sock = os_get_option("TERMUX_VA_SOCKET");
    if (env_sock && *env_sock)
        return env_sock;

    const char *env_dir = os_get_option("TERMUX_VA_SOCKET_DIR");
    if (env_dir && *env_dir) {
        snprintf(buf, bufsz, "%s/%s", env_dir, TVA_SOCK_NAME);
        return buf;
    }

    /* The daemon applies a termux-x11-style fallback chain to TMPDIR;
     * mirror it, then prefer whichever default path actually exists. */
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp || !strcmp(tmp, "/data/local/tmp"))
        tmp = NULL;

    if (tmp) {
        snprintf(buf, bufsz, "%s/%s/%s", tmp, TVA_SOCKET_DIR_NAME, TVA_SOCK_NAME);
        struct stat st;
        if (stat(buf, &st) == 0 && S_ISSOCK(st.st_mode))
            return buf;
    }
    snprintf(buf, bufsz, "/tmp/%s/%s", TVA_SOCKET_DIR_NAME, TVA_SOCK_NAME);
    return buf;
}

int tva_format_display_width(const struct tva_format *fmt)
{
    if (!fmt || !fmt->valid)
        return 0;
    int w = fmt->crop_right - fmt->crop_left + 1;
    return w > 0 ? w : 0;
}

int tva_format_display_height(const struct tva_format *fmt)
{
    if (!fmt || !fmt->valid)
        return 0;
    int h = fmt->crop_bottom - fmt->crop_top + 1;
    return h > 0 ? h : 0;
}

struct tva_session *tva_session_create(const struct tva_session_config *cfg,
                                       struct tva_error *err)
{
    if (err) {
        memset(err, 0, sizeof(*err));
    }
    if (!cfg) {
        err_set(err, TVA_ERR_INVAL, 0, "cfg is NULL", 0);
        return NULL;
    }
    if (cfg->codec < CODEC_H264 || cfg->codec >= CODEC_MAX) {
        err_set(err, TVA_ERR_INVAL, 0, "invalid codec id", 0);
        return NULL;
    }
    if (cfg->width < 96 || cfg->height < 96 ||
        cfg->width > 8192 || cfg->height > 4320) {
        err_set(err, TVA_ERR_INVAL, 0,
                "resolution outside the daemon range 96x96..8192x4320", 0);
        return NULL;
    }

    struct tva_session *s = calloc(1, sizeof(*s));
    if (!s) {
        err_set(err, TVA_ERR_NOMEM, 0, "session allocation failed", 0);
        return NULL;
    }
    s->fd = -1;
    s->log = tva_log_wanted();
    s->codec = cfg->codec;
    s->io_timeout_ms = cfg->io_timeout_ms > 0 ? cfg->io_timeout_ms : TVA_DEF_IO_MS;
    s->xfer = XFER_INLINE;

    int cto = cfg->connect_timeout_ms > 0 ? cfg->connect_timeout_ms
                                          : TVA_DEF_CONNECT_MS;

    char defep[300];
    const char *path = cfg->sock_path ? cfg->sock_path
                                      : tva_default_endpoint(defep, sizeof(defep));
    if (unix_connect(s, path, cto, err) < 0) {
        if (err)
            s->err = *err;
        tva_session_destroy(s);
        return NULL;
    }
    if (do_handshake(s, cfg, HELLO_VERSION, err) < 0) {
        /*
         * Version downgrade retry: daemons that check the version strictly
         * reject v3 with status=1.  Retry once with v2 (whose response has
         * no endpoint extension; the inode check is skipped).  Only a
         * version rejection downgrades - codec/resolution rejections are
         * unaffected by the version.
         */
        if (s->err.code == TVA_ERR_REJECTED && s->err.handshake_status == 1 &&
            HELLO_VERSION > TVA_VERSION_MIN) {
            tva_c_log(s, "daemon does not accept protocol v%u, retrying with v2"
                         " (inode check will be skipped)",
                      (unsigned)HELLO_VERSION);
            if (s->fd >= 0) {
                close(s->fd);        /* the daemon closed it; reconnect */
                s->fd = -1;
            }
            s->ep_path[0] = '\0';
            memset(&s->err, 0, sizeof(s->err));
            if (unix_connect(s, path, cto, err) < 0 ||
                do_handshake(s, cfg, TVA_VERSION_MIN, err) < 0) {
                if (err)
                    s->err = *err;
                tva_session_destroy(s);
                return NULL;
            }
        } else {
            if (err)
                s->err = s->err;
            tva_session_destroy(s);
            return NULL;
        }
    }

    tva_c_log(s, "session established: unix=%s codec=%d %dx%d frame-delivery=%s",
              s->ep_path, cfg->codec, cfg->width, cfg->height,
              s->xfer == XFER_SHM ? "SHM" : "inline");
    return s;
}

void tva_session_cancel(struct tva_session *s)
{
    if (!s)
        return;
    __atomic_store_n(&s->cancelled, 1, __ATOMIC_RELEASE);
    if (s->fd >= 0)
        shutdown(s->fd, SHUT_RDWR);
}

void tva_session_destroy(struct tva_session *s)
{
    if (!s)
        return;
    if (s->shm_base) {
        /* Return every still-held slot: the daemon may still be waiting for
         * them and holding slots makes it wait out its timeout. */
        for (int i = 0; i < s->shm_slots; i++)
            __atomic_store_n(shm_state_word(s, i), 0u, __ATOMIC_RELEASE);
        munmap(s->shm_base, s->shm_total);
        s->shm_base = NULL;
    }
    if (s->fd >= 0) {
        close(s->fd);
        s->fd = -1;
    }
    free(s->rbuf);
    s->rbuf = NULL;
    free(s);
}

int tva_session_send_unit(struct tva_session *s, const void *data, size_t len)
{
    if (!s)
        return TVA_ERR_INVAL;
    if (!data || len == 0)
        return sess_err(s, TVA_ERR_INVAL, "empty data unit", 0);
    if (len > TVA_MAX_UNIT_BYTES)
        return sess_err(s, TVA_ERR_TOOBIG,
                        "data unit exceeds the daemon's 8MB cap", 0);
    if (s->fd < 0)
        return sess_err(s, TVA_ERR_STATE, "session has no live connection", 0);
    /* A previous send failed after the length prefix went out: the uplink
     * byte stream is misaligned and further sends would feed the daemon
     * data as lengths.  Refuse and force a session rebuild. */
    if (s->tx_broken)
        return sess_err(s, TVA_ERR_STATE,
                        "uplink corrupted (earlier send interrupted); rebuild the session", 0);

    /* H.264/HEVC units MUST carry an Annex B start code: the daemon locates
     * the nal_unit_header through it to recognize SPS/PPS/VPS.  Missing
     * start codes make the decoder silently swallow everything and produce
     * no frames - fail here instead.  VP8/VP9 is the opposite: adding start
     * codes would corrupt the frame, so none are ever added. */
    if (s->codec == CODEC_H264 || s->codec == CODEC_HEVC) {
        const uint8_t *b = data;
        int sc3 = (len >= 3 && b[0] == 0 && b[1] == 0 && b[2] == 1);
        int sc4 = (len >= 4 && b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1);
        if (!sc3 && !sc4)
            return sess_err(s, TVA_ERR_PROTOCOL,
                            "H.264/HEVC data unit lacks an Annex B start code", 0);
    }

    uint32_t be = htonl((uint32_t)len);
    int r = send_exact(s, &be, 4, s->io_timeout_ms);
    if (r != TVA_OK) {
        /* the length prefix itself may be partially written: stream broken */
        s->tx_broken = 1;
        return r;
    }
    r = send_exact(s, data, len, s->io_timeout_ms);
    if (r != TVA_OK) {
        s->tx_broken = 1;
        return r;
    }

    s->units_sent++;
    return TVA_OK;
}

static int ensure_rbuf(struct tva_session *s, size_t need)
{
    if (s->rbuf_size >= need)
        return TVA_OK;
    size_t ns = need + need / 2;
    if (ns < 256 * 1024)
        ns = 256 * 1024;
    uint8_t *nb = realloc(s->rbuf, ns);
    if (!nb)
        return sess_err(s, TVA_ERR_NOMEM, "failed to grow the receive buffer", 0);
    s->rbuf = nb;
    s->rbuf_size = ns;
    return TVA_OK;
}

/* Stamp the current format snapshot onto the frame so every frame carries
 * self-consistent geometry. */
static void frame_apply_format(const struct tva_session *s, struct tva_frame *f)
{
    f->unit_seq     = s->last_pts;
    f->stride       = s->fmt.stride;
    f->slice_height = s->fmt.slice_height;
    f->crop_left    = s->fmt.crop_left;
    f->crop_top     = s->fmt.crop_top;
    f->crop_right   = s->fmt.crop_right;
    f->crop_bottom  = s->fmt.crop_bottom;
}

int tva_session_next_frame(struct tva_session *s, struct tva_frame *out,
                           int timeout_ms)
{
    if (!s)
        return TVA_ERR_INVAL;
    if (__atomic_load_n(&s->cancelled, __ATOMIC_ACQUIRE))
        return TVA_ERR_CANCELLED;
    if (!out)
        return sess_err(s, TVA_ERR_INVAL, "out is NULL", 0);
    if (s->fd < 0)
        return sess_err(s, TVA_ERR_STATE, "session has no live connection", 0);
    if (s->eos)
        return TVA_EOS;
    if (s->rbuf_busy)
        return sess_err(s, TVA_ERR_STATE,
                        "previous frame not released, the receive buffer is still held", 0);

    int first_to = (timeout_ms < 0) ? s->io_timeout_ms : timeout_ms;
    int rest_to  = s->io_timeout_ms;

    /* Loop: the header may introduce a format block; consume it and keep
     * waiting for a real frame */
    for (;;) {
        uint8_t hdr[12];
        int r = recv_exact(s, hdr, sizeof(hdr), first_to, rest_to);
        if (r != TVA_OK)
            return r;                /* TVA_EOS / TIMEOUT / errors as-is */

        /* byte-wise memcpy + ntohl: hdr is uint8_t[]; a direct uint32_t*
         * dereference may be unaligned on aarch64 (undefined behavior) */
        uint32_t w_be, h_be, sz_be;
        memcpy(&w_be, hdr + 0, 4);
        memcpy(&h_be, hdr + 4, 4);
        memcpy(&sz_be, hdr + 8, 4);
        uint32_t w = ntohl(w_be), h = ntohl(h_be), sz = ntohl(sz_be);

        if (sz == FMTDESC_SENTINEL) {
            /* word 2 is the capability flags (0 on legacy daemons) */
            s->caps = h;
            /* [0][caps][0xFFFFFFFF] is followed by 8 big-endian words */
            uint32_t fw[FMTDESC_WORDS];
            r = recv_exact(s, fw, sizeof(fw), rest_to, rest_to);
            if (r == TVA_EOS)
                return sess_err(s, TVA_ERR_PROTOCOL, "format block truncated", 0);
            if (r != TVA_OK)
                return r;
            s->fmt.buf_width    = (int)ntohl(fw[0]);
            s->fmt.buf_height   = (int)ntohl(fw[1]);
            s->fmt.stride       = (int)ntohl(fw[2]);
            s->fmt.slice_height = (int)ntohl(fw[3]);
            s->fmt.crop_left    = (int)ntohl(fw[4]);
            s->fmt.crop_top     = (int)ntohl(fw[5]);
            s->fmt.crop_right   = (int)ntohl(fw[6]);
            s->fmt.crop_bottom  = (int)ntohl(fw[7]);
            s->fmt.valid = 1;
            s->fmt.changes++;
            tva_c_log(s, "format block #%d: buffer %dx%d stride=%d slice=%d display %dx%d",
                      s->fmt.changes, s->fmt.buf_width, s->fmt.buf_height,
                      s->fmt.stride, s->fmt.slice_height,
                      tva_format_display_width(&s->fmt),
                      tva_format_display_height(&s->fmt));
            /* bytes already arrived: keep using rest_to so a probe-style
             * short timeout cannot misjudge a half-read block */
            first_to = rest_to;
            continue;
        }

        if (sz == SHMFRAME_SENTINEL) {
            /* SHM control message: [slot][length] follow the 12-byte head,
             * plus the unit-index word when the daemon announced PTS */
            uint32_t si[2];
            r = recv_exact(s, si, sizeof(si), rest_to, rest_to);
            if (r == TVA_EOS)
                return sess_err(s, TVA_ERR_PROTOCOL, "SHM control message truncated", 0);
            if (r != TVA_OK)
                return r;
            int slot = (int)ntohl(si[0]);
            uint32_t dlen = ntohl(si[1]);

            if (s->caps & CAP_FRAME_PTS) {
                uint32_t p_be;
                r = recv_exact(s, &p_be, 4, rest_to, rest_to);
                if (r == TVA_EOS)
                    return sess_err(s, TVA_ERR_PROTOCOL,
                                    "SHM message PTS field truncated", 0);
                if (r != TVA_OK)
                    return r;
                s->last_pts = ntohl(p_be);
            } else {
                s->last_pts = 0;
            }

            if (!s->shm_base)
                return sess_err(s, TVA_ERR_PROTOCOL,
                                "SHM frame received but shared memory not attached", 0);
            if (slot < 0 || slot >= s->shm_slots)
                return sess_err(s, TVA_ERR_PROTOCOL, "SHM slot out of range", 0);
            if ((size_t)dlen > s->shm_slot_bytes)
                return sess_err(s, TVA_ERR_PROTOCOL, "SHM frame exceeds the slot size", 0);

            memset(out, 0, sizeof(*out));
            out->data = s->shm_base + SHM_CTRL_BYTES
                      + (size_t)slot * s->shm_slot_bytes;
            out->size = dlen;
            out->width = w;
            out->height = h;
            out->unit_seq = s->last_pts;
            out->shm_slot = slot;
            out->seq = s->frames_recv;
            frame_apply_format(s, out);
            s->frames_recv++;
            s->shm_held++;
            return TVA_OK;
        }

        /* Plain inline frame.  A daemon with CAP_FRAME_PTS sends one extra
         * word after the 12-byte header: the input unit index.  It MUST be
         * consumed before the frame body or the stream misaligns. */
        if (s->caps & CAP_FRAME_PTS) {
            uint32_t p_be;
            r = recv_exact(s, &p_be, 4, first_to, rest_to);
            if (r == TVA_EOS)
                return sess_err(s, TVA_ERR_PROTOCOL, "frame header PTS field truncated", 0);
            if (r != TVA_OK)
                return r;
            s->last_pts = ntohl(p_be);
        } else {
            s->last_pts = 0;   /* 0 = no PTS info */
        }

        if (sz == 0)
            return sess_err(s, TVA_ERR_PROTOCOL, "frame length is 0", 0);
        if (sz > TVA_MAX_FRAME_BYTES || w > 16384 || h > 16384)
            return sess_err(s, TVA_ERR_PROTOCOL, "unreasonable frame header values", 0);

        r = ensure_rbuf(s, sz);
        if (r != TVA_OK)
            return r;
        r = recv_exact(s, s->rbuf, sz, rest_to, rest_to);
        if (r == TVA_EOS)
            return sess_err(s, TVA_ERR_PROTOCOL, "frame data truncated", 0);
        if (r != TVA_OK)
            return r;

        memset(out, 0, sizeof(*out));
        out->data = s->rbuf;
        out->size = sz;
        out->width = w;
        out->height = h;
        out->shm_slot = -1;
        out->seq = s->frames_recv;
        frame_apply_format(s, out);
        s->frames_recv++;
        s->rbuf_busy = 1;
        return TVA_OK;
    }
}

int tva_session_drain(struct tva_session *s)
{
    if (!s)
        return TVA_ERR_INVAL;
    if (s->fd < 0)
        return sess_err(s, TVA_ERR_STATE, "session has no live connection", 0);
    if (s->tx_broken)
        return sess_err(s, TVA_ERR_STATE,
                        "uplink corrupted (earlier send interrupted); rebuild the session", 0);

    /* length 0 = reversible drain: the daemon queues EOS, flushes the
     * decoder, re-sends the CSD and the session stays alive. */
    uint32_t be = htonl(0);
    int r = send_exact(s, &be, 4, s->io_timeout_ms);
    if (r != TVA_OK) {
        s->tx_broken = 1;
        return r;
    }
    return TVA_OK;
}

int tva_session_release_frame(struct tva_session *s, struct tva_frame *f)
{
    if (!s)
        return TVA_ERR_INVAL;
    if (!f || !f->data)
        return TVA_OK;              /* unconditional release is safe */

    if (f->shm_slot >= 0) {
        if (!s->shm_base || f->shm_slot >= s->shm_slots)
            return sess_err(s, TVA_ERR_INVAL, "invalid slot in release", 0);
        /* release ordering: all reads of the frame data happen before the
         * daemon sees the state word reset and overwrites the slot */
        __atomic_store_n(shm_state_word(s, f->shm_slot), 0u, __ATOMIC_RELEASE);
        if (s->shm_held > 0)
            s->shm_held--;
    } else {
        s->rbuf_busy = 0;
    }

    f->data = NULL;
    f->size = 0;
    f->shm_slot = -1;
    return TVA_OK;
}

const struct tva_format *tva_session_format(const struct tva_session *s)
{
    return s ? &s->fmt : NULL;
}

const char *tva_session_last_error(const struct tva_session *s)
{
    if (!s)
        return "session is NULL";
    return s->err.msg;
}

int tva_session_last_error_code(const struct tva_session *s)
{
    return s ? s->err.code : TVA_ERR_INVAL;
}

int tva_session_xfer_mode(const struct tva_session *s)
{
    return s ? s->xfer : XFER_INLINE;
}

uint64_t tva_session_units_sent(const struct tva_session *s)
{
    return s ? s->units_sent : 0;
}

uint64_t tva_session_frames_received(const struct tva_session *s)
{
    return s ? s->frames_recv : 0;
}
