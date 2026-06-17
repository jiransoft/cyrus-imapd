/* om_ipcheck.h -- OfficeMail per-domain client-IP allowlist check
 *
 * Copyright (c) 2026 Jiransoft.  All rights reserved.
 *
 * Part of the OfficeMail cyrus-imapd fork; distributed under the same
 * license terms as Cyrus IMAP.
 */

#ifndef OM_IPCHECK_H
#define OM_IPCHECK_H

#include <sasl/sasl.h>

/* Check the client IP of an authenticated connection against the
 * per-domain allowlist maintained by the OfficeMail verifier daemon.
 * Returns SASL_OK when the IP is allowed or the check does not apply
 * (option disabled, admin user, no remote IP, exempt CIDR, or any
 * verifier error - fail-open), SASL_NOAUTHZ when the verifier denies
 * the IP.
 */
int om_ipcheck_authorize(sasl_conn_t *conn, const char *userid,
                         int userisadmin);

/* httpd only: set the effective client IP (bare IP, no port or
 * brackets) to be used instead of SASL_IPREMOTEPORT for subsequent
 * checks in this process.  Pass NULL to clear the override.
 */
void om_ipcheck_set_client_ip(const char *ip);

/* Returns nonzero if 'ip' (bare IPv4/IPv6 address) is inside any of
 * the networks in the whitespace-separated 'cidr_list' (bare addresses
 * count as /32 or /128).  An empty or NULL list never matches.
 */
int om_ipcheck_ip_in_cidrs(const char *ip, const char *cidr_list);

/* Returns nonzero if the most recent om_ipcheck_authorize() call in this
 * process denied the client IP.  httpd reads this after authentication to
 * turn the generic 401 into a 403 with a distinguishing response header so
 * a client can tell an IP-allowlist denial apart from bad/expired
 * credentials.  Cleared per request by om_ipcheck_clear_denied().
 */
int om_ipcheck_was_denied(void);

/* Clear the per-process IP-denied flag.  Callers (httpd) invoke this once
 * at the start of each request so the verdict never leaks across requests
 * on a persistent connection.
 */
void om_ipcheck_clear_denied(void);

#endif /* OM_IPCHECK_H */
