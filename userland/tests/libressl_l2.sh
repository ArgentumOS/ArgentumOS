#!/bin/sh
# libressl_l2.sh — L2's acceptance: THE FSH TRUST STORE DECIDES, AND IT DECIDES BOTH WAYS.
# docs/design/libressl-plan.md §4 and §5/L2.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHAT L2 OWES, in the plan's words: "a real TLS fetch (https) verifies a real certificate against the
# FNX store and succeeds/fails on trust exactly as configured". So this script does the whole thing
# LOCALLY and deterministically — no external network, no dependence on what any real CA has signed:
#
#   1. it MAKES a CA and a server certificate that CA signed (SAN: localhost and 127.0.0.1);
#   2. it INSTALLS the CA at the FSH store, `/System/Configuration/SSL/cert.pem` — the file LibreSSL's
#      compiled-in OPENSSLDIR points at, and the file this build of curl was given as `CURL_CA_BUNDLE`;
#   3. it runs `openssl s_server` on loopback and `curl https://127.0.0.1:PORT/`, which must SUCCEED;
#   4. it REPLACES the store's CA with a different one and repeats, which must be REFUSED.
#
# STEP 4 IS THE ONE THAT MAKES IT A TEST OF POLICY RATHER THAN OF A HANDSHAKE: a store that was ignored
# would pass step 3 only by accident and could not pass step 4 at all.
#
# IT WRITES TO A SYSTEM FILE, deliberately (the store is System content) and puts back what it found,
# which in a freshly staged image is nothing.
#
# AND `openssl version -d` IS MEASURED rather than assumed: it prints the COMPILED OPENSSLDIR, so the
# check that the store path is the compiled one is a measurement of the artifact, not of this script's
# intentions.
#
# ORDER MATTERS IN HERE, AND IT IS THE ORDER OF A SENTENCE: make the keys, install the CA, start the
# server, fetch (trusted), fetch (refused), and only THEN show that another client can reach it. The
# discriminator at the top is what this script used to do, and it poisoned the fetches it was meant to
# explain — see its own comment.
#
# Output: LIBRESSL-L2 <check> ok|FAIL <detail>, then RESULT/STATUS/DONE.

DIR="/System/Temporary Files/libressl-l2"
# THE FULL PATH, because System/Tools is not on a bare PATH here. curl IS a System/Tools binary: its
# Linux paths were PATCHED to ours (third_party/curl-fsh.patch) rather than excused, so it passes the FSH
# lint gate on its merits.
CURL="/System/Tools/curl"
STORE="/System/Configuration/SSL"
PORT=46466
okc=0
failc=0

check() {	# name ok detail
	if [ "$2" = "1" ]; then
		okc=$((okc + 1))
		echo "LIBRESSL-L2 $1 ok"
	else
		failc=$((failc + 1))
		echo "LIBRESSL-L2 $1 FAIL $3"
	fi
}

rm -rf "$DIR"
mkdir -p "$DIR" || {
	echo "LIBRESSL-L2 setup FAIL cannot create $DIR"
	echo "LIBRESSL-L2 RESULT ok=0 fail=1"
	echo "LIBRESSL-L2-STATUS=1"
	echo "LIBRESSL-L2 DONE"
	exit 1
}
cd "$DIR" || exit 1

# PUT BACK WHAT WE FIND. In a freshly staged image the store holds no cert.pem; if it holds one, it is
# moved aside and restored at the end, because this script must not be the reason a system file changed.
RESTORE=0
if [ -f "$STORE/cert.pem" ]; then
	cp "$STORE/cert.pem" saved-cert.pem
	RESTORE=1
fi

# --- 1. A CA, AND A SERVER CERTIFICATE THAT CA SIGNED ------------------------------------------
openssl req -x509 -newkey rsa:2048 -keyout ca.key -out ca.pem -days 1 -nodes \
	-subj /CN=FNX-L2-Test-CA > ca.log 2>&1
if [ -s ca.pem ] && [ -s ca.key ]; then
	check ca-generated 1 ""
else
	check ca-generated 0 "$(sed -n '1,3p' ca.log)"
fi

openssl req -newkey rsa:2048 -keyout srv.key -out srv.csr -nodes \
	-subj /CN=localhost > csr.log 2>&1
# A SAN IS REQUIRED, not decoration: modern verification matches the NAME against the subjectAltName
# extension, and a certificate whose only name is a CN would be refused for the right reason but the
# wrong test. The assertion below greps for "subject alternative name" WITH SPACES, because that is how
# LibreSSL RENDERS it in `-text` — the first version grepped for the extension's own spelling and
# reported a failure whose detail said "Signature ok".
printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' > san.cnf
openssl x509 -req -in srv.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out srv.pem -days 1 \
	-extfile san.cnf > sign.log 2>&1
openssl x509 -in srv.pem -noout -text > srv.txt 2>&1
if [ -s srv.pem ] && grep -qi 'subject alternative name' srv.txt; then
	check server-certificate-signed-by-the-ca 1 ""
else
	check server-certificate-signed-by-the-ca 0 "$(sed -n '1,3p' sign.log)"
fi

# --- 2. THE STORE, AND THE PATH THE ARTIFACT WAS COMPILED WITH ---------------------------------
OPENSSLDIR_LINE="$(openssl version -d 2>&1)"
case "$OPENSSLDIR_LINE" in
	*"$STORE"*)
		check openssldir-is-the-fsh-store 1 "" ;;
	*)
		check openssldir-is-the-fsh-store 0 "openssl version -d said: $OPENSSLDIR_LINE" ;;
esac

mkdir -p "$STORE" || true
cp ca.pem "$STORE/cert.pem"
SUBJ="$(openssl x509 -in "$STORE/cert.pem" -noout -subject 2>&1)"
case "$SUBJ" in
	*FNX-L2-Test-CA*)
		check ca-installed-in-the-fsh-store 1 "" ;;
	*)
		check ca-installed-in-the-fsh-store 0 "the store's subject is: $SUBJ" ;;
esac

# --- 3. THE SERVER ------------------------------------------------------------------------------
# `-no_dhe` IS NOT DECORATION: s_server defaults to "auto DH parameters" (s_server.c:1284), which
# LibreSSL GENERATES ON DEMAND AT THE FIRST HANDSHAKE — minutes of arithmetic on the single thread that
# would otherwise be accepting. So the first connection was accepted and then sat in a handshake nobody
# could finish, the client timed out, and every later attempt stayed unaccepted in the backlog.
# `-no_dhe` drops the DHE path; nothing here needs it and TLS 1.3 does not use it at all.
openssl s_server -accept "$PORT" -cert srv.pem -key srv.key -www -no_dhe > srv.log 2>&1 &
SRV=$!

# A SHORT READINESS WAIT THAT TOUCHES NOTHING. s_server prints `ACCEPT` once, AHEAD of do_server()
# (s_server.c:1404, with a BIO_flush behind it), so it means "about to listen" rather than "listening" —
# which is fine, because the refusal retry below covers the gap and costs the server nothing.
#
# THREE WAYS THIS SCRIPT USED TO POKE THE SERVER, ALL WRONG, ALL FIXED HERE:
#   * a PLAIN HTTP request at the listener: s_server accepts it, waits for a ClientHello that never
#     comes, and never accepts again — the readiness probe sabotaged the thing it was waiting for;
#   * RETRYING THE FETCH ON A TIMEOUT: that attempt had already been accepted, so the single-threaded
#     server was left wedged while the next attempt sat unaccepted in the backlog (POLLOUT is correctly
#     not reported for a socket nobody has accepted). The retry is on REFUSAL (7) ONLY, where nothing
#     was ever connected;
#   * a 60-try version of this wait ate an entire ~70s harness window, so the fetch the case exists for
#     never ran at all.
wait_ready() {
	tries=0
	while [ "$tries" -lt 5 ]; do
		if grep -q ACCEPT srv.log; then
			return 0
		fi
		sleep 1
		tries=$((tries + 1))
	done
	return 1
}
wait_ready || echo "LIBRESSL-L2-DIAG the server has not printed ACCEPT yet (the fetch will retry on refusal)"
echo "LIBRESSL-L2-DIAG s_server pid=$SRV log:"
sed -n '1,10p' srv.log

# ONE FETCH, WITH A SHELL-LEVEL BOUND AROUND CURL ITSELF.
#
# `--max-time` IS NOT ENOUGH, AND THAT IS MEASURED: a curl stuck somewhere its own timeout cannot reach
# eats the whole harness window and reports NOTHING — no check line, no trace, just a killed VM. So curl
# runs in the background, the wait is bounded here, and whatever it was doing gets dumped either way.
# A check that cannot report is not a check.
fetch() {	# url outfile -> FETCH_RC / FETCH_OUT
	"$CURL" -sS -v --max-time 8 -o "$2" \
		-w 'code=%{http_code} verify=%{ssl_verify_result}' "$1" \
		> fetch.out 2> verbose.txt &
	CURLPID=$!
	i=0
	while [ "$i" -lt 12 ]; do
		if ! kill -0 "$CURLPID" > killcheck.log 2>&1; then
			break
		fi
		sleep 1
		i=$((i + 1))
	done
	if kill -0 "$CURLPID" > killcheck.log 2>&1; then
		echo "LIBRESSL-L2-DIAG curl was STILL RUNNING after ${i}s although --max-time is 8 - killing it"
		kill "$CURLPID" > kill.log 2>&1
		wait "$CURLPID" > kill.log 2>&1
		FETCH_RC=124
	else
		wait "$CURLPID"
		FETCH_RC=$?
	fi
	FETCH_OUT="$(sed -n '1,3p' fetch.out)"
}

# A RETRY ON REFUSAL ONLY, because a refusal is the one failure that PROVES nothing was connected.
fetch_ready() {	# url outfile
	tries=0
	while [ "$tries" -lt 20 ]; do
		fetch "$1" "$2"
		if [ "$FETCH_RC" != "7" ]; then
			break
		fi
		sleep 1
		tries=$((tries + 1))
	done
}

# NO --cacert, ON PURPOSE: the whole point is that the DEFAULTS resolve to the FSH store, which is what
# this build wired at compile time. A test that named the file would prove nothing about the wiring.
fetch_ready "https://127.0.0.1:$PORT/" body.txt
TRUSTED="$FETCH_OUT"
TRUSTED_RC="$FETCH_RC"

# CURL'S OWN NARRATION, because it is the only thing that can say WHERE a stalled transfer stopped.
echo "LIBRESSL-L2-DIAG curl -v (first 25 lines):"
sed -n '1,25p' verbose.txt

case "$TRUSTED" in
	*code=200*verify=0*)
		check https-verifies-against-the-store 1 "" ;;
	*)
		check https-verifies-against-the-store 0 "curl exit=$TRUSTED_RC said: $TRUSTED" ;;
esac

# --- 4. THE SAME FETCH, WITH A CA THE STORE DOES NOT HOLD, AND IT MUST BE REFUSED ---------------
# NO RETRY HERE, and that is the point: the server is up by now, so the ONLY acceptable outcome is the
# certificate refusal. A retry would blur a timeout into a success.
echo "LIBRESSL-L2-DIAG starting the untrusted half"
openssl req -x509 -newkey rsa:2048 -keyout other.key -out other.pem -days 1 -nodes \
	-subj /CN=Some-Other-CA > other.log 2>&1
cp other.pem "$STORE/cert.pem"
echo "LIBRESSL-L2-DIAG the store now holds a CA the server's certificate was NOT signed by"

# `-o untrusted-body.txt`, NOT /dev/null: this FSH has none (the null device is @null), and a redirect
# to it is itself an error the shell reports.
fetch "https://127.0.0.1:$PORT/" untrusted-body.txt
UNTRUSTED="$FETCH_OUT"
UNTRUSTED_RC="$FETCH_RC"

# CURLE_PEER_FAILED_VERIFICATION IS 60, AND NOTHING ELSE WILL DO. An earlier version accepted ANY
# non-zero exit, which a TIMEOUT (28) satisfies — a check that passes because the test was too slow is
# not a check. The refusal asserted here is the certificate one.
if [ "$UNTRUSTED_RC" = "60" ]; then
	check an-untrusted-ca-is-refused 1 ""
else
	check an-untrusted-ca-is-refused 0 \
		"curl exit $UNTRUSTED_RC is NOT the verification refusal (60): $UNTRUSTED"
fi

echo "LIBRESSL-L2-DIAG s_server log, after the fetches (did it accept?):"
sed -n '1,20p' srv.log

# --- 5. THE DISCRIMINATOR, LAST SO IT CANNOT PERTURB A CHECK ------------------------------------
# THE QUESTION THAT NAMED THE BUG: can a DIFFERENT client reach the SAME server? Its answer is on the
# record — the server is fine; s_client connects, handshakes and verifies against our test CA — and it
# runs HERE, after every fetch, because it ENDS BY BEING KILLED, and killing a client mid-handshake is
# exactly what wedges a single-threaded server. Run first, it failed the fetches and then blamed curl.
#
# AND s_client IS THE BACKGROUND JOB ITSELF (no subshell): killing a subshell leaves s_client running
# and the server holding a connection nobody will ever finish — the same wedge by another route.
echo "LIBRESSL-L2-DIAG DISCRIMINATOR: can s_client reach the same server?"
printf 'GET / HTTP/1.0\r\n\r\n' > req.txt
openssl s_client -connect "127.0.0.1:$PORT" -CAfile srv.pem -ign_eof \
	< req.txt > scli.log 2>&1 &
SCLI=$!
i=0
while [ "$i" -lt 8 ]; do
	if ! kill -0 "$SCLI" > killcheck.log 2>&1; then
		break
	fi
	sleep 1
	i=$((i + 1))
done
if kill -0 "$SCLI" > killcheck.log 2>&1; then
	echo "LIBRESSL-L2-DIAG s_client still running after ${i}s (it waits for the server to close) - killing it"
	kill "$SCLI" > kill.log 2>&1
	wait "$SCLI" > kill.log 2>&1
	SCLI_RC=124
else
	wait "$SCLI"
	SCLI_RC=$?
fi
echo "LIBRESSL-L2-DIAG s_client exit=$SCLI_RC after ${i}s"
grep -E 'CONNECTED|verify return|Certificate chain|^ [0-9] s:|^   i:|error' scli.log | head -6

echo "LIBRESSL-L2-DIAG stopping the server"
kill "$SRV" 2>&1 || true
echo "LIBRESSL-L2-DIAG server stopped"

# --- 6. PUT THE STORE BACK, AND SAY WHAT HAPPENED ----------------------------------------------
if [ "$RESTORE" = "1" ]; then
	cp saved-cert.pem "$STORE/cert.pem"
else
	rm -f "$STORE/cert.pem"
fi

echo "LIBRESSL-L2-DIAG trusted:   rc=$TRUSTED_RC $TRUSTED"
echo "LIBRESSL-L2-DIAG untrusted: rc=$UNTRUSTED_RC $UNTRUSTED"
echo "LIBRESSL-L2-DIAG untrusted body: $(sed -n '1,2p' untrusted-body.txt)"
echo "LIBRESSL-L2 RESULT ok=$okc fail=$failc"
echo "LIBRESSL-L2-STATUS=$([ "$failc" = "0" ] && echo 0 || echo 1)"
echo "LIBRESSL-L2 DONE"
[ "$failc" = "0" ] || exit 1
exit 0
