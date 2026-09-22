#!/bin/sh
# libressl_l1.sh — L1's in-guest acceptance: a REAL TLS HANDSHAKE on FNX.
# docs/design/libressl-plan.md §5/L1.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# THIS IS A SHELL SCRIPT RATHER THAN A C PROBE, and deliberately: what L1 owes is not a library call but
# TWO PROCESSES TALKING — `openssl s_server` on one side and `openssl s_client` on the other — which is
# how the plan words it, and which is what makes it an acceptance for the OS rather than for a symbol.
# The handshake exercises entropy (the key and the nonces), the socket layer, and the whole TLS state
# machine, with NO EXTERNAL NETWORK: it is loopback only.
#
# ONE THING IT DOES NOT PROVE, STATED SO IT IS NOT MISTAKEN FOR COVERAGE: the trust store. This uses
# `-CAfile cert.pem` — the certificate the script itself just made — so it proves the HANDSHAKE and not
# the verification POLICY. The store itself is L2's; what L1 owes it is the ground it stands on, and
# that is now in place: the build compiles OPENSSLDIR to `/System/Configuration/ssl`, so LibreSSL's
# default CA file is `/System/Configuration/ssl/cert.pem` — no Linux path anywhere.
#
# EVERY STEP IS BOUNDED, because the first run of this script HUNG here (71s, killed by the harness) and
# a probe that can hang is a probe that cannot report. The client is run in the background under a
# counted wait and killed if it outstays it, and every step narrates its exit status on a -DIAG line.
#
# NO /dev/null ANYWHERE, and that is a fact about this system rather than a style: FSH has none (the
# null device is @null = /System/Devices/null), so a `2>/dev/null` redirect is itself an error the shell
# reports. Redirects go to files in the work directory instead, which is also where the diagnostics want
# them.
#
# Output: LIBRESSL-L1 <check> ok|FAIL <detail>, then RESULT/STATUS/DONE, the house probe shape.

DIR="/System/Temporary Files/libressl-l1"
PORT=4443
CLIENT_WAIT=40		# seconds the client may take before it is treated as hung
okc=0
failc=0

check() {	# name ok detail
	if [ "$2" = "1" ]; then
		okc=$((okc + 1))
		echo "LIBRESSL-L1 $1 ok"
	else
		failc=$((failc + 1))
		echo "LIBRESSL-L1 $1 FAIL $3"
	fi
}

diag() {
	echo "LIBRESSL-L1-DIAG $1"
}

rm -rf "$DIR"
if ! mkdir -p "$DIR"; then
	echo "LIBRESSL-L1 setup FAIL cannot create $DIR"
	echo "LIBRESSL-L1 RESULT ok=0 fail=1"
	echo "LIBRESSL-L1-STATUS=1"
	echo "LIBRESSL-L1 DONE"
	exit 1
fi
cd "$DIR" || exit 1

# --- 1. THE TOOL RUNS AT ALL -------------------------------------------------------------------
VER="$(openssl version 2>&1)"
# A SUBSTRING, NOT AN EQUALITY: the version is one line of what openssl prints, and matching the whole
# output would make the check depend on every other line.
if echo "$VER" | grep -q 'LibreSSL 4.3.2'; then
	check openssl-runs 1 ""
else
	check openssl-runs 0 "openssl version said: $VER"
fi
diag "openssl version -> $VER"

# --- 2. A CERTIFICATE GENERATED ON THE GUEST (the entropy question) -----------------------------
diag "cwd=$(pwd)"
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 1 -nodes \
	-subj /CN=localhost > req.log 2>&1
REQ=$?
diag "openssl req exit=$REQ files=[$(ls)]"
sed -n '1,12p' req.log

SUBJ="$(openssl x509 -in cert.pem -noout -subject 2>&1)"
case "$SUBJ" in
	*CN*=*localhost*)
		check self-signed-cert-generated 1 "" ;;
	*)
		check self-signed-cert-generated 0 "subject=($SUBJ) req.log=($(tail -3 req.log))" ;;
esac

# --- 3. THE SERVER, IN THE BACKGROUND, ITS OUTPUT TO A FILE ------------------------------------
# NOT to the console: the harness reads the serial console, and a server chattering into it would
# corrupt every other probe's output. The log is read afterwards instead.
openssl s_server -accept "$PORT" -cert cert.pem -key key.pem -www > srv.log 2>&1 &
SRV=$!
sleep 3
diag "s_server pid=$SRV started; its first lines follow"
sed -n '1,6p' srv.log

# --- 4. THE CLIENT: one HTTP GET THROUGH TLS, UNDER A BOUNDED WAIT -----------------------------
# `-state` AND SPLIT STREAMS, because the first run of this step produced ONE line and was killed:
# s_client's session block goes to STDOUT, which is block-buffered into a file and therefore LOST when
# the process is killed, while `-state` writes each handshake step to STDERR as it happens. Keeping them
# apart is what makes a stall diagnosable instead of invisible.
( printf 'GET / HTTP/1.0\r\n\r\n' | openssl s_client -state -connect "127.0.0.1:$PORT" \
	-CAfile cert.pem -verify_return_error > cli.log 2> cli.err ) &
CLI_PID=$!
i=0
while [ "$i" -lt "$CLIENT_WAIT" ]; do
	if ! kill -0 "$CLI_PID" > killcheck.log 2>&1; then
		break
	fi
	sleep 1
	i=$((i + 1))
done
if kill -0 "$CLI_PID" > killcheck.log 2>&1; then
	diag "s_client STILL RUNNING after ${i}s - killing it (a hang here is a handshake that never finished)"
	kill "$CLI_PID" > kill.log 2>&1 || true
	CLI=124
else
	wait "$CLI_PID"
	CLI=$?
fi
diag "s_client exit=$CLI after ${i}s"

kill "$SRV" > kill.log 2>&1 || true
wait "$SRV" > wait.log 2>&1 || true

diag "s_client stdout (first 30 lines):"
sed -n '1,30p' cli.log
diag "s_client stderr/-state (first 40 lines):"
sed -n '1,40p' cli.err
diag "s_server output (first 10 lines):"
sed -n '1,10p' srv.log

if [ -s cli.log ]; then
	if grep -q 'Verify return code: 0 (ok)' cli.log; then
		check client-verified-the-certificate 1 ""
	else
		check client-verified-the-certificate 0 \
			"$(grep -i 'verify return code\|verify error' cli.log | head -2)"
	fi

	if grep -qE 'Protocol *: *TLSv1\.[23]' cli.log; then
		check tls-version-selected 1 ""
	else
		check tls-version-selected 0 "$(grep -i 'protocol *:' cli.log | head -1)"
	fi

	if grep -qi 'HTTP/1.0 200' cli.log; then
		check application-data-flowed 1 ""
	else
		check application-data-flowed 0 "no HTTP 200 in the client's bytes"
	fi
else
	check client-verified-the-certificate 0 "s_client produced no output (exit $CLI)"
	check tls-version-selected 0 "s_client produced no output"
	check application-data-flowed 0 "s_client produced no output"
fi

# WHAT THIS PROVES IS THE TCP ACCEPT AND NOT A HANDSHAKE — it asserted `ACCEPT` and passed on a run
# where the client hung mid-handshake, which is a check passing for the wrong reason. It is named for
# what the evidence supports; the handshake claims rest on the client's session block above.
if grep -q 'ACCEPT' srv.log; then
	check server-accepted-the-connection 1 ""
else
	check server-accepted-the-connection 0 "s_server log: $(head -2 srv.log)"
fi

echo "LIBRESSL-L1 RESULT ok=$okc fail=$failc"
echo "LIBRESSL-L1-STATUS=$([ "$failc" = "0" ] && echo 0 || echo 1)"
echo "LIBRESSL-L1 DONE"
[ "$failc" = "0" ] || exit 1
exit 0
