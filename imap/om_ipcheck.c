/* om_ipcheck.c -- OfficeMail per-domain client-IP allowlist check
 *
 * Copyright (c) 2026 Jiransoft.  All rights reserved.
 *
 * Part of the OfficeMail cyrus-imapd fork; distributed under the same
 * license terms as Cyrus IMAP.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "libconfig.h"
#include "om_ipcheck.h"
#include "util.h"
#include "xstrlcpy.h"

/* effective client IP set by httpd (X-Forwarded-For); empty = unset */
static char client_ip_override[INET6_ADDRSTRLEN+1] = "";

/* set when the last authorize() denied the client IP; read by httpd to
   emit a 403 + distinguishing header instead of a generic 401 */
static int om_ipcheck_denied = 0;

EXPORTED int om_ipcheck_was_denied(void)
{
    return om_ipcheck_denied;
}

EXPORTED void om_ipcheck_clear_denied(void)
{
    om_ipcheck_denied = 0;
}

EXPORTED void om_ipcheck_set_client_ip(const char *ip)
{
    if (ip && *ip)
        strlcpy(client_ip_override, ip, sizeof(client_ip_override));
    else
        client_ip_override[0] = '\0';
}

/* "a.b.c.d;993" or "2001:db8::1;993" -> bare IP (strip at LAST ';',
   which is IPv6-safe).  Returns 0 on success, -1 otherwise. */
static int strip_port(const char *ipport, char *out, size_t outlen)
{
    const char *semi = strrchr(ipport, ';');
    size_t n = semi ? (size_t) (semi - ipport) : strlen(ipport);

    if (!n || n >= outlen) return -1;
    memcpy(out, ipport, n);
    out[n] = '\0';

    return 0;
}

/* Parse a bare IPv4/IPv6 address into a 128-bit value.  IPv4 addresses
   are stored in v4-mapped form (::ffff:a.b.c.d), which also normalizes
   v4-mapped IPv6 input.  Returns the native prefix length of the
   address family (32 or 128), or -1 if the address does not parse. */
static int parse_addr128(const char *s, unsigned char addr[16])
{
    unsigned char v4[4];

    if (inet_pton(AF_INET, s, v4) == 1) {
        memset(addr, 0, 10);
        addr[10] = addr[11] = 0xff;
        memcpy(addr + 12, v4, 4);
        return 32;
    }
    if (inet_pton(AF_INET6, s, addr) == 1) return 128;

    return -1;
}

/* Compare the leading 'bits' bits of two 128-bit addresses. */
static int prefix_match(const unsigned char *a, const unsigned char *b,
                        int bits)
{
    int nbytes = bits / 8;
    int nbits = bits % 8;

    if (nbytes && memcmp(a, b, nbytes)) return 0;
    if (nbits) {
        unsigned char mask = (unsigned char) (0xff << (8 - nbits));

        if ((a[nbytes] & mask) != (b[nbytes] & mask)) return 0;
    }

    return 1;
}

/* A v4-mapped address (::ffff:a.b.c.d) represents an IPv4 address; a plain
   IPv6 address does not.  Used to gate matching by family. */
static int is_v4mapped(const unsigned char a[16])
{
    static const unsigned char pfx[12] =
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };

    return memcmp(a, pfx, sizeof(pfx)) == 0;
}

EXPORTED int om_ipcheck_ip_in_cidrs(const char *ip, const char *cidr_list)
{
    unsigned char client[16];
    const char *p;
    int client_v4;

    if (!ip || !*ip || !cidr_list || !*cidr_list) return 0;
    if (parse_addr128(ip, client) < 0) return 0;
    client_v4 = is_v4mapped(client);

    p = cidr_list;
    while (*p) {
        char tok[64], *slash;
        unsigned char net[16];
        size_t n;
        int native, bits;

        while (*p && Uisspace(*p)) p++;
        if (!*p) break;
        n = strcspn(p, " \t\r\n");
        if (!n || n >= sizeof(tok)) {
            p += n;
            continue;
        }
        memcpy(tok, p, n);
        tok[n] = '\0';
        p += n;

        slash = strchr(tok, '/');
        if (slash) *slash++ = '\0';

        native = parse_addr128(tok, net);
        if (native < 0) continue;

        if (slash) {
            char *end = NULL;
            long prefix;

            /* require plain digits after '/': reject an empty prefix
               and the '+'/'-' signs that strtol would accept */
            if (!Uisdigit(*slash)) continue;
            prefix = strtol(slash, &end, 10);
            if (end == slash || *end || prefix < 0 || prefix > native)
                continue;
            /* IPv4 prefixes shift into the v4-mapped address space */
            bits = (native == 32) ? (int) prefix + 96 : (int) prefix;
        }
        else {
            bits = 128;     /* bare address = exact match */
        }

        /* Family gate: a v4-mapped network counts as IPv4 only when its mask
           reaches the embedded octets (>= /96); otherwise it is IPv6.  Only
           compare networks of the same family as the client, so a v4-mapped
           IPv4 client never matches a sub-/96 v4-mapped or plain-IPv6 network
           (matching the Python verifier's per-family behavior). */
        if (client_v4 != (is_v4mapped(net) && bits >= 96)) continue;

        if (prefix_match(client, net, bits)) return 1;
    }

    return 0;
}

/* Send "ipcheck <userid> <ip>\n" to the verifier daemon and read the
   single-line reply (trailing whitespace trimmed).  Returns 0 on
   success, -1 on any socket/protocol failure (caller fails open). */
static int query_verifier(const char *userid, const char *ip,
                          char *reply, size_t replylen)
{
    const char *sockpath = config_getstring(IMAPOPT_OM_IPCHECK_SOCKET);
    struct sockaddr_un sun_data;
    struct timeval tv = { 3, 0 };   /* I/O timeout */
    char request[1024];
    int len, soc = -1;
    ssize_t n;

    if (!sockpath || !*sockpath) {
        syslog(LOG_ERR, "om_ipcheck: om_ipcheck_socket is not set");
        return -1;
    }

    len = snprintf(request, sizeof(request), "ipcheck %s %s\n", userid, ip);
    if (len < 0 || (size_t) len >= sizeof(request)) {
        syslog(LOG_ERR, "om_ipcheck: request too long for user <%s>", userid);
        return -1;
    }

    memset((char *)&sun_data, 0, sizeof(sun_data));
    sun_data.sun_family = AF_UNIX;
    strlcpy(sun_data.sun_path, sockpath, sizeof(sun_data.sun_path));

    soc = socket(PF_UNIX, SOCK_STREAM, 0);
    if (soc < 0) {
        syslog(LOG_ERR, "om_ipcheck: unable to create socket(): %m");
        return -1;
    }

    setsockopt(soc, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(soc, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Non-blocking connect bounded by the same timeout: a wedged verifier
       (not calling accept()) fills the listen backlog, and a plain blocking
       connect() would then hang with no bound -- SO_SNDTIMEO covers send,
       not connect -- defeating the intended fail-open-within-timeout. */
    {
        int flags = fcntl(soc, F_GETFL, 0);
        int rc, soerr = 0;
        socklen_t slen = sizeof(soerr);
        int timeout_ms = (int) (tv.tv_sec * 1000 + tv.tv_usec / 1000);

        if (flags < 0 || fcntl(soc, F_SETFL, flags | O_NONBLOCK) < 0) {
            syslog(LOG_ERR, "om_ipcheck: fcntl failed on %s: %m", sockpath);
            close(soc);
            return -1;
        }

        rc = connect(soc, (struct sockaddr *)&sun_data, sizeof(sun_data));
        if (rc < 0 && errno == EINPROGRESS) {
            struct pollfd pfd;
            int pr;

            pfd.fd = soc;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            do {
                pr = poll(&pfd, 1, timeout_ms);
            } while (pr < 0 && errno == EINTR);

            if (pr <= 0) {
                syslog(LOG_ERR, "om_ipcheck: connect to %s timed out", sockpath);
                close(soc);
                return -1;
            }
            if (getsockopt(soc, SOL_SOCKET, SO_ERROR, &soerr, &slen) < 0 ||
                soerr != 0) {
                syslog(LOG_ERR, "om_ipcheck: failed to connect to %s: %s",
                       sockpath, strerror(soerr ? soerr : errno));
                close(soc);
                return -1;
            }
        }
        else if (rc < 0) {
            syslog(LOG_ERR, "om_ipcheck: failed to connect to %s: %m", sockpath);
            close(soc);
            return -1;
        }

        /* restore blocking mode so write/read honor SO_SNDTIMEO/SO_RCVTIMEO */
        if (fcntl(soc, F_SETFL, flags) < 0) {
            syslog(LOG_ERR, "om_ipcheck: fcntl restore failed on %s: %m", sockpath);
            close(soc);
            return -1;
        }
    }

    /* retry on EINTR; the 3s socket timeouts cap each blocking call */
    do {
        n = write(soc, request, len);
    } while (n < 0 && errno == EINTR);
    if (n != len) {
        syslog(LOG_ERR, "om_ipcheck: failed to write to %s: %m", sockpath);
        close(soc);
        return -1;
    }

    do {
        n = read(soc, reply, replylen - 1);
    } while (n < 0 && errno == EINTR);
    if (n == 0) {
        /* EOF before any reply (errno is not meaningful here) */
        syslog(LOG_ERR, "om_ipcheck: connection to %s closed without reply",
               sockpath);
        close(soc);
        return -1;
    }
    if (n < 0) {
        syslog(LOG_ERR, "om_ipcheck: failed to read from %s: %m", sockpath);
        close(soc);
        return -1;
    }
    close(soc);

    reply[n] = '\0';
    while (n > 0 && Uisspace(reply[n-1])) reply[--n] = '\0';

    return 0;
}

EXPORTED int om_ipcheck_authorize(sasl_conn_t *conn, const char *userid,
                                  int userisadmin)
{
    char ip[INET6_ADDRSTRLEN+1] = "";
    const char *exempt;
    const char *q;
    char reply[256];

    if (!config_getswitch(IMAPOPT_OM_IPCHECK)) return SASL_OK;
    if (userisadmin) return SASL_OK;

    if (client_ip_override[0]) {
        strlcpy(ip, client_ip_override, sizeof(ip));
    }
    else {
        const void *val = NULL;

        /* no remote IP (e.g. unix socket / lmtpd): nothing to check */
        if (sasl_getprop(conn, SASL_IPREMOTEPORT, &val) != SASL_OK || !val)
            return SASL_OK;
        if (strip_port((const char *) val, ip, sizeof(ip))) return SASL_OK;
    }
    if (!*ip) return SASL_OK;

    exempt = config_getstring(IMAPOPT_OM_IPCHECK_EXEMPT_CIDRS);
    if (exempt && om_ipcheck_ip_in_cidrs(ip, exempt)) return SASL_OK;

    /* userids containing whitespace or control characters would corrupt
       the single-line wire protocol: skip the query and fail open */
    for (q = userid; *q; q++) {
        if (*q == ' ' || (unsigned char) *q < 0x20) {
            syslog(LOG_WARNING, "om_ipcheck: userid contains unsafe "
                   "characters, skipping check ip=<%s> service=<%s>",
                   ip, config_ident);
            return SASL_OK;
        }
    }

    if (query_verifier(userid, ip, reply, sizeof(reply)) < 0) {
        syslog(LOG_ERR, "om_ipcheck: verifier unavailable, FAILING OPEN "
               "user=<%s> ip=<%s> service=<%s>", userid, ip, config_ident);
        return SASL_OK;
    }

    if (!strcmp(reply, "OK")) return SASL_OK;

    if (!strncmp(reply, "DENY", 4)) {
        syslog(LOG_NOTICE,
               "om_ipcheck: DENY user=<%s> ip=<%s> service=<%s> [%s]",
               userid, ip, config_ident, reply);
        sasl_seterror(conn, 0, "client IP %s not allowed for %s", ip, userid);
        om_ipcheck_denied = 1;
        return SASL_NOAUTHZ;
    }

    syslog(LOG_ERR, "om_ipcheck: unexpected verifier reply '%s', FAILING OPEN "
           "user=<%s> ip=<%s>", reply, userid, ip);
    return SASL_OK;
}
