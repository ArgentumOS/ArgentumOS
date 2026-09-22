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
	check ca-generated 0 "$(tail -3 ca.log)"
fi

openssl req -newkey rsa:2048 -keyout srv.key -out srv.csr -nodes \
	-subj /CN=localhost > csr.log 2>&1
# A SAN IS REQUIRED, not decoration: modern verification matches the NAME against the
# subjectAltName extension, and a certificate whose only name is a CN would be refused for the right
# reason but the wrong test. The assertion below greps for "subject alternative name" WITH SPACES,
# because that is how LibreSSL RENDERS it in `-text` — the first version of this script grepped for the
# extension's own spelling and reported a failure with "Signature ok" in its detail.
printf 'subjectAltName=DNS:localhost,IP:127.0.0.1\n' > san.cnf
openssl x509 -req -in srv.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out srv.pem -days 1 \
	-extfile san.cnf > sign.log 2>&1
openssl x509 -in srv.pem -noout -text > srv.txt 2>&1
if [ -s srv.pem ] && grep -qi 'subject alternative name' srv.txt; then
	check server-certificate-signed-by-the-ca 1 ""
else
	check server-certificate-signed-by-the-ca 0 "$(tail -3 sign.log)"
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

# --- 3. THE SERVER, AND THE FETCH THAT MUST SUCCEED --------------------------------------------
openssl s_server -accept "$PORT" -cert srv.pem -key srv.key -www > srv.log 2>&1 &
SRV=$!
sleep 2
# ITS OWN LOG, DUMPED: the first run of this script found the server NOT LISTENING and had no way to say
# why, because the only place s_server would have explained itself was a file nobody read.
echo "LIBRESSL-L2-DIAG s_server pid=$SRV log:"
sed -n '1,10p' srv.log

# THE SERVER TAKES A WHILE TO LISTEN, AND A SLEEP CANNOT KNOW HOW LONG. Its own log says why: `Using
# auto DH parameters` — LibreSSL generates them at startup, which on this guest outlasts any fixed sleep
# (the first run measured curl refused at 50 ms and the same server answering later). So the wait is a
# BOUNDED RETRY on the real fetch, and `--max-time` bounds every transfer, so nothing here can hang.
fetch() {	# url outfile -> FETCH_RC / FETCH_OUT
	tries=0
	while [ "$tries" -lt 20 ]; do
		FETCH_OUT="$("$CURL" -sS --max-time 10 -o "$2" \
			-w 'code=%{http_code} verify=%{ssl_verify_result}' "$1" 2>&1)"
		FETCH_RC=$?
		if [ "$FETCH_RC" = "7" ]; then		# CURLE_COULDNT_CONNECT: not listening YET
			sleep 1
			tries=$((tries + 1))
			continue
		fi
		break
	done
}

# NO --cacert, ON PURPOSE: the whole point is that the DEFAULTS resolve to the FSH store, which is what
# this build wired at compile time. A test that named the file would prove nothing about the wiring.
fetch "https://127.0.0.1:$PORT/" body.txt
TRUSTED="$FETCH_OUT"
TRUSTED_RC="$FETCH_RC"

case "$TRUSTED" in
	*code=200*verify=0*)
		check https-verifies-against-the-store 1 "" ;;
	*)
		check https-verifies-against-the-store 0 "curl exit=$TRUSTED_RC said: $TRUSTED" ;;
esac

# --- 4. THE SAME FETCH, WITH A CA THE STORE DOES NOT HOLD, AND IT MUST BE REFUSED ---------------
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

if [ "$UNTRUSTED_RC" != "0" ]; then
	check an-untrusted-ca-is-refused 1 ""
else
	check an-untrusted-ca-is-refused 0 "curl ACCEPTED a certificate the store does not hold: $UNTRUSTED"
fi

echo "LIBRESSL-L2-DIAG stopping the server"
kill "$SRV" 2>&1 || true
echo "LIBRESSL-L2-DIAG server stopped"

# --- 5. PUT THE STORE BACK, AND SAY WHAT HAPPENED ----------------------------------------------
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
