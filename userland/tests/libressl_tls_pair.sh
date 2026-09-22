#!/bin/sh
# libressl_tls_pair.sh — generate a certificate ON THE GUEST, then run the libtls handshake pair.
# docs/design/libressl-plan.md §5/L1.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# THE CERTIFICATE IS MADE HERE AND THE HANDSHAKE IS MADE IN C, and the split is deliberate: `openssl
# req` is the tool the plan names for key generation (and the entropy question), while the handshake
# belongs to `libressl_tls_pair.c`, which uses the FIRST-PARTY libtls API over BLOCKING sockets. That
# program exists separately because `openssl s_client` MULTIPLEXES — it selects on the socket and on
# stdin — so its stall in libressl_l1.sh has two readings, and this pair removes one of them.
#
# NO /dev/null ANYWHERE (this FSH has none — the null device is @null), and no `tr` (the guest's toybox
# has none). Both cost a run to learn.

DIR="/System/Temporary Files/libressl-pair"
PROBE="/System/Shared/tests/libressl_tls_pair"

rm -rf "$DIR"
mkdir -p "$DIR" || {
	echo "LIBRESSL-PAIR setup FAIL cannot create $DIR"
	exit 1
}
cd "$DIR" || exit 1

openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 1 -nodes \
	-subj /CN=localhost > req.log 2>&1
REQ=$?
if [ "$REQ" != "0" ] || [ ! -s cert.pem ] || [ ! -s key.pem ]; then
	echo "LIBRESSL-PAIR-DIAG certificate generation failed (exit $REQ, files: $(ls))"
	sed -n '1,12p' req.log
fi

# exec, so the probe owns stdout from here: it prints the checks, the RESULT tally and DONE, and a
# wrapper that printed its own checks would have to keep a second tally in step with the first.
exec "$PROBE" cert.pem key.pem
