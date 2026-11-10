#!/bin/bash
# test_tls_duplex against a local openssl s_server -rev, TLS 1.3 and 1.2.
#   tls_duplex_test.sh ./test_tls_duplex
set -e
TOOL=$1
D=$(mktemp -d); SRV=
trap 'kill $SRV 2>/dev/null || true; rm -rf $D' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -keyout $D/key.pem -out $D/cert.pem \
   -days 2 -subj "/CN=localhost" 2>/dev/null
for v in 1_3 1_2; do
   openssl s_server -rev -accept 44351 -cert $D/cert.pem -key $D/key.pem -tls$v >/dev/null 2>&1 &
   SRV=$!
   # until it listens, five seconds at most
   n=0
   until (exec 3<>/dev/tcp/127.0.0.1/44351) 2>/dev/null || [ $n -ge 50 ]; do
      n=$((n + 1)); sleep 0.1
   done
   $TOOL localhost 44351 400
   kill $SRV 2>/dev/null || true; wait $SRV 2>/dev/null || true
done
