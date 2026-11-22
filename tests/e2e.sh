#!/bin/bash
# End-to-end test of every menu item: runs ./stocksocial with scripted input and
# checks the output (and sometimes the database directly).
#
# Usage: make && tests/e2e.sh
#
# Needs Postgres running and the historical data loaded (it checks known AAPL,
# MSFT and BHF prices). Creates fresh users (qa1_<n>, qa2_<n>, qa3_<n>, password
# "pw") and a test symbol (Q<n>) each run, and leaves them in the database.
# Uses the same PG* environment variables as the app.
#
# Menu numbers used below:
#   start      1 Register  2 Log in
#   main       1 Portfolios  2 Stocks  3 Friends  4 Stock lists  5 Log out
#   portfolios 1 List  2 Create  3 Open
#   portfolio  1 View  2 Deposit  3 Withdraw  4 Transfer  5 Buy  6 Sell  7 Holding history
#   stocks     1 Price history  2 Add daily price
#   friends    1 View  2 Incoming  3 Outgoing  4 Send  5 Remove
#   lists      1 View  2 Create  3 Open
#   list       1 View  2 Reviews  3 Write review  4 Delete review
#              (creator only) 5 Set stock  6 Remove stock  7 Visibility  8 Share  9 Unshare  10 Delete

cd "$(dirname "$0")/.." || exit 1
TS=$(date +%s); T=${TS: -6}
A=qa1_$T; B=qa2_$T; C=qa3_$T; SYM=Q$T
PSQL="env PGPASSWORD=${PGPASSWORD:-postgres} psql -h ${PGHOST:-127.0.0.1} -p ${PGPORT:-5432} -U ${PGUSER:-postgres} -d stocksocial -X -At"
command -v psql >/dev/null || PSQL=${PSQL/ psql / /data/pg19/bin/psql }
pass=0; fail=0

# check NAME INPUT REGEX: runs the app with INPUT, expects REGEX in the output.
check() {
    local out; out=$(printf '%b' "$2" | ./stocksocial 2>&1); local code=$?
    if grep -qE "$3" <<<"$out" && ! grep -q 'Database error' <<<"$out" && [ $code -eq 0 ]; then
        pass=$((pass+1)); echo "PASS  $1"
    else
        fail=$((fail+1)); echo "FAIL  $1 (exit $code)"
        echo "$out" | grep -vE '^(== |[0-9]+\. |$)' | tail -6 | sed 's/^/      /'
    fi
}

# absent NAME INPUT REGEX: runs the app with INPUT, expects REGEX NOT in the output.
absent() {
    local out; out=$(printf '%b' "$2" | ./stocksocial 2>&1)
    if ! grep -qE "$3" <<<"$out" && ! grep -q 'Database error' <<<"$out"; then
        pass=$((pass+1)); echo "PASS  $1"
    else
        fail=$((fail+1)); echo "FAIL  $1"
    fi
}

# count NAME INPUT REGEX N: expects exactly N output lines matching REGEX.
count() {
    local n; n=$(printf '%b' "$2" | ./stocksocial 2>&1 | grep -cE "$3")
    if [ "$n" -eq "$4" ]; then
        pass=$((pass+1)); echo "PASS  $1"
    else
        fail=$((fail+1)); echo "FAIL  $1: expected $4 lines matching [$3], got $n"
    fi
}

# sql NAME QUERY EXPECTED: exact match on the query's output.
sql() {
    local got; got=$($PSQL -c "$2" 2>&1)
    if [ "$got" == "$3" ]; then
        pass=$((pass+1)); echo "PASS  $1"
    else
        fail=$((fail+1)); echo "FAIL  $1: expected [$3] got [$got]"
    fi
}

login() { echo "2\n$1\npw\n"; }

# Position of list L1 in USER's "Open list" menu (same ORDER BY as the app), so
# the test doesn't depend on which other lists exist.
l1() {
    $PSQL -c "SELECT n FROM (SELECT id, row_number() OVER (ORDER BY creator <> '$1', creator, name) AS n
              FROM lists WHERE can_access(id, '$1')) x
              WHERE id = (SELECT id FROM lists WHERE creator = '$A' AND name = 'L1')"
}
open_l1() { echo "4\n3\n$(l1 $1)\n"; }

echo "== Concurrent startup"
codes=$(for i in 1 2 3 4 5 6; do (./stocksocial </dev/null >/dev/null 2>&1; echo $?) & done; wait)
if [ "$(tr -d '\n' <<<"$codes")" == "000000" ]; then pass=$((pass+1)); echo "PASS  6 clients start at once"
else fail=$((fail+1)); echo "FAIL  6 clients start at once: exit codes $(tr '\n' ' ' <<<"$codes")"; fi
sql   "schema hash recorded"        "SELECT count(*) FROM schema_version" "1"

echo "== Start menu / auth ($A, $B, $C)"
check "register A"                  "1\n$A\npw\npw\n"                    "Registered $A"
check "register B, C"               "1\n$B\npw\npw\n1\n$C\npw\npw\n"     "Registered $C"
check "register taken"              "1\n$A\npw\npw\n"                    "Username already taken"
check "register blank username"     "1\n   \n"                           "1 to 32 characters"
check "register 33-char username"   "1\n$(printf 'x%.0s' {1..33})\n"     "1 to 32 characters"
check "register empty password"     "1\nqa_x$T\n\n"                      "can't be empty"
check "register mismatch"           "1\nqa_x$T\npw\npx\n"                "don't match"
check "login ok (trimmed name)"     "2\n  $A  \npw\n"                    "Logged in as $A"
check "login bad password"          "2\n$A\nnope\n"                      "Invalid username or password"
check "login unknown user"          "2\nnobody_$T\npw\n"                 "Invalid username or password"
check "invalid menu input"          "9\nabc\n-1\n 0 \n"                  "Enter a whole number from 0 to 2"
check "Ctrl+D exits cleanly"        "$(login $A)1\n"                     "Portfolios"
check "logout then login other"     "$(login $A)5\n$(login $B)"          "Logged in as $B"

echo "== Portfolios"
check "list with none"              "$(login $A)1\n1\n"                  "You have no portfolios"
check "open with none"              "$(login $A)1\n3\n"                  "You have no portfolios"
check "create Main, Side"           "$(login $A)1\n2\nMain\n2\nSide\n"   "Created portfolio Side"
check "create duplicate"            "$(login $A)1\n2\nMain\n"            "already have a portfolio named Main"
check "create blank name"           "$(login $A)1\n2\n\n"                "1 to 64 characters"
check "create 65-char name"         "$(login $A)1\n2\n$(printf 'x%.0s' {1..65})\n" "1 to 64 characters"
check "open cancel"                 "$(login $A)1\n3\n0\n1\n"            "Main"
check "view empty"                  "$(login $A)1\n3\n1\n1\n"            "Cash: 0.00"
check "deposit invalid formats"     "$(login $A)1\n3\n1\n2\n-5\n1.234\nabc\n0\n0.00\n1e5\n1000\n" "Deposited 1000"
sql   "deposit stored exactly"      "SELECT cash FROM portfolios WHERE username='$A' AND name='Main'" "1000.00"
check "deposit max in message"     "$(login $A)1\n3\n1\n2\n10000000000000\n"  "at most 9999999999999.99"
check "deposit cents"               "$(login $A)1\n3\n1\n2\n0.05\n"      "Deposited 0.05"
check "deposit overflow"            "$(login $A)1\n3\n1\n2\n9999999999999.99\n" "Balance would be too large"
check "withdraw too much"           "$(login $A)1\n3\n1\n3\n5000\n"      "Not enough cash"
check "withdraw ok"                 "$(login $A)1\n3\n1\n3\n0.05\n"      "Withdrew 0.05"
check "transfer to self"            "$(login $A)1\n3\n1\n4\n1\n"         "Choose a different portfolio"
check "transfer cancel"             "$(login $A)1\n3\n1\n4\n0\n1\n"      "Cash: 1000.00"
check "transfer too much"           "$(login $A)1\n3\n1\n4\n2\n5000\n"   "Not enough cash"
check "transfer ok"                 "$(login $A)1\n3\n1\n4\n2\n100\n"    "Transferred 100 to Side"
sql   "transfer balances"           "SELECT string_agg(name||'='||cash, ',' ORDER BY name) FROM portfolios WHERE username='$A'" "Main=900.00,Side=100.00"
check "buy lowercase"               "$(login $A)1\n3\n1\n5\naapl\n2\n"   "Bought 2 AAPL for 319.08"
check "buy again adds"              "$(login $A)1\n3\n1\n5\nAAPL\n1\n"   "Bought 1 AAPL"
sql   "holding quantity summed"     "SELECT quantity FROM holds WHERE username='$A' AND symbol='AAPL'" "3"
check "buy unknown"                 "$(login $A)1\n3\n1\n5\nZZZZZZ\n1\n" "Unknown symbol ZZZZZZ"
check "buy not enough cash"         "$(login $A)1\n3\n1\n5\nAAPL\n100\n" "Not enough cash"
check "buy huge quantity"           "$(login $A)1\n3\n1\n5\nAAPL\n2147483647\n" "Not enough cash"
check "view with holdings"          "$(login $A)1\n3\n1\n1\n"            "AAPL +3 +159.54 +478.62"
check "list totals"                 "$(login $A)1\n1\n"                  "Main +421.38 +900.00"
check "sell too many"               "$(login $A)1\n3\n1\n6\nAAPL\n4\n"   "You only own 3 shares of AAPL"
check "sell not held"               "$(login $A)1\n3\n1\n6\nMSFT\n1\n"   "You only own 0 shares of MSFT"
check "sell unknown"                "$(login $A)1\n3\n1\n6\nZZZZZZ\n1\n" "Unknown symbol ZZZZZZ"
check "sell partial"                "$(login $A)1\n3\n1\n6\nAAPL\n1\n"   "Sold 1 AAPL for 159.54"
check "holding history"             "$(login $A)1\n3\n1\n7\n1\n2\n\n"    "AAPL close, 2018-01-08 to 2018-02-07 \(22 trading days\)"
check "holding history cancel"      "$(login $A)1\n3\n1\n7\n0\n1\n"      "Cash:"
check "holding history none"        "$(login $A)1\n3\n2\n7\n"            "No stock holdings"
check "sell all deletes row"        "$(login $A)1\n3\n1\n6\nAAPL\n2\n"   "Sold 2 AAPL"
sql   "no zero-quantity rows"       "SELECT count(*) FROM holds WHERE username='$A'" "0"
sql   "cash back after round trip"  "SELECT cash FROM portfolios WHERE username='$A' AND name='Main'" "900.00"

echo "== Stocks"
for i in 1 2 3 4 5; do
check "history interval $i"         "$(login $A)2\n1\nMSFT\n$i\n\n"      "MSFT close, .* trading days\)"
done
check "history 5y range"            "$(login $A)2\n1\nAAPL\n5\n\n"       "2013-02-08 to 2018-02-07 \(1259 trading days\)"
check "history table"               "$(login $A)2\n1\nAAPL\n1\ny\n"      "2018-02-07 +163.085"
check "history table, uppercase Y"  "$(login $A)2\n1\nAAPL\n1\nY\n"      "2018-02-07 +163.085"
check "history NULL open in table"  "$(login $A)2\n1\nBHF\n4\nyes\n"     "2017-07-26 + 69.0842"
check "history unknown symbol"      "$(login $A)2\n1\nZZZZZZ\n1\n"       "No price data for ZZZZZZ"
check "add: new symbol declined"    "$(login $A)2\n2\n$SYM\nn\n"         "new symbol"
sql   "declined symbol not created" "SELECT count(*) FROM stocks WHERE symbol='$SYM'" "0"
check "add: symbol too long"        "$(login $A)2\n2\nABCDEFGHI\n"       "1 to 8 characters"
check "add: invalid date, asked first" "$(login $A)2\n2\nAAPL\n2018-02-30\n" "Invalid date 2018-02-30"
check "add: future date"           "$(login $A)2\n2\nAAPL\n2099-01-01\n" "can't be in the future"
check "add: new symbol (Y), blanks" "$(login $A)2\n2\n$SYM\nY\n2018-02-07\n\n\n\n10\n100\n" "Added $SYM close 10 on 2018-02-07"
sql   "blanks stored as NULL"       "SELECT coalesce(open::text,'NULL'), close FROM daily_prices WHERE symbol='$SYM'" "NULL|10"
count "add: zero and bad decimals rejected" "$(login $A)2\n2\n$SYM\n2018-02-08\nabc\n0\n11\n12\n10.5\n0.0\n12\n500\n" "Enter a positive number" 3
sql   "...then added"               "SELECT close FROM daily_prices WHERE symbol='$SYM' AND \"timestamp\"='2018-02-08'" "12"
check "add: low above high"         "$(login $A)2\n2\n$SYM\n2018-02-09\n\n10\n12\n11\n1\n" "low <= open, close <= high"
check "add: close above high"       "$(login $A)2\n2\n$SYM\n2018-02-09\n\n10\n\n11\n1\n" "low <= open, close <= high"
check "add: open below low"         "$(login $A)2\n2\n$SYM\n2018-02-09\n4\n\n5\n6\n1\n" "low <= open, close <= high"
sql   "rejected prices not stored"  "SELECT count(*) FROM daily_prices WHERE symbol='$SYM' AND \"timestamp\"='2018-02-09'" "0"
check "add: duplicate date"         "$(login $A)2\n2\n$SYM\n2018-02-08\n\n\n\n1\n1\n" "already has a price for 2018-02-08"
check "add: huge number"            "$(login $A)2\n2\n$SYM\n2018-02-09\n\n\n\n1$(printf '0%.0s' {1..400})\n1\n" "A price is too large"
check "history of new symbol (2 pts)" "$(login $A)2\n1\n$SYM\n5\n\n"     "2018-02-07 to 2018-02-08 \(2 trading days\)"
check "buy new symbol at latest"    "$(login $A)1\n3\n1\n5\n$SYM\n10\n"  "Bought 10 $SYM for 120.00"

echo "== Friends"
check "view none"                   "$(login $A)3\n1\n"                  "You have no friends yet"
check "incoming none"               "$(login $A)3\n2\n"                  "No incoming requests"
check "outgoing none"               "$(login $A)3\n3\n"                  "No outgoing requests"
check "remove with none"            "$(login $A)3\n5\n"                  "You have no friends yet"
check "send to self"                "$(login $A)3\n4\n$A\n"              "to yourself"
check "send to unknown"             "$(login $A)3\n4\nnobody_$T\n"       "No user named"
check "send A->B, A->C"             "$(login $A)3\n4\n$B\n4\n$C\n3\n"    "  $C"
check "send duplicate"              "$(login $A)3\n4\n$B\n"              "already sent"
check "B incoming, cancel"          "$(login $B)3\n2\n1\n0\n2\n"         "$A"
check "B accepts"                   "$(login $B)3\n2\n1\n1\n1\n"         "now friends with $A"
check "send when friends"           "$(login $A)3\n4\n$B\n"              "already friends"
check "C rejects"                   "$(login $C)3\n2\n1\n2\n"            "Rejected request from $A"
check "A on cooldown"               "$(login $A)3\n4\n$C\n"              "another request in [0-9]+ seconds"
check "C (rejecter) can send"       "$(login $C)3\n4\n$A\n"              "Sent friend request to $A"
check "A sends back = auto-accept"  "$(login $A)3\n4\n$C\n"              "you are now friends"
check "A views friends"             "$(login $A)3\n1\n"                  "  $B"

echo "== Stock lists"
absent "B sees no L1 yet"           "$(login $B)4\n1\n"                  "L1 \\["
check "create blank"                "$(login $A)4\n2\n\n"                "1 to 64 characters"
check "create L1"                   "$(login $A)4\n2\nL1\n"              "Created private list L1"
check "view empty list"             "$(login $A)$(open_l1 $A)1\n"        "No stocks in this list"
check "set stocks + unknown + update" "$(login $A)$(open_l1 $A)5\nAAPL\n10\n5\nmsft\n5\n5\nZZZZZZ\n1\n5\nAAPL\n20\n1\n" "AAPL +20 +159.54 +3190.80"
check "remove stock cancel/ok"      "$(login $A)$(open_l1 $A)6\n0\n6\n2\n1\n" "Removed MSFT"
absent "private hidden from friend" "$(login $B)4\n1\n"                  "L1 \\["
check "share with B -> shared"      "$(login $A)$(open_l1 $A)8\n1\n1\n"  "Shared with: $B"
check "share again (no dup)"        "$(login $A)$(open_l1 $A)8\n1\n"     "Shared L1 with $B"
sql   "one share row"               "SELECT count(*) FROM shared_with sw JOIN lists l ON l.id=sw.list_id WHERE l.creator='$A'" "1"
check "B sees shared list"          "$(login $B)4\n1\n"                  "L1 \[shared\] by $A"
check "B gets read-only menu"       "$(login $B)$(open_l1 $B)5\n"        "Enter a whole number from 0 to 4"
absent "C (not shared) can't see"   "$(login $C)4\n1\n"                  "L1 \\["
check "unshare cancel"              "$(login $A)$(open_l1 $A)9\n0\n"     "Unshare with"
check "set visibility public"       "$(login $A)$(open_l1 $A)7\n3\n"     "L1 is now public"
check "C sees public list"          "$(login $C)4\n1\n"                  "L1 \[public\] by $A"
check "share while public"          "$(login $A)$(open_l1 $A)8\n2\n"     "public, so everyone can see it"

echo "== Reviews"
check "reviews none"                "$(login $B)$(open_l1 $B)2\n"        "No reviews you can see"
check "B writes multi-line"         "$(login $B)$(open_l1 $B)3\nline one\nline two\n\n2\n" "  line one"
check "B edits (shows old)"         "$(login $B)$(open_l1 $B)3\nedited\n\n" "Your current review"
sql   "edit replaced, one row"      "SELECT count(*), max(content) FROM reviews r JOIN lists l ON l.id=r.list_id WHERE l.creator='$A'" "1|edited"
check "write cancel"                "$(login $C)$(open_l1 $C)3\n\n"      "Cancelled"
check "too long"                    "$(login $C)$(open_l1 $C)3\n$(printf 'x%.0s' {1..4001})\n\n" "at most 4000"
check "C writes"                    "$(login $C)$(open_l1 $C)3\nfrom C\n\n" "Saved your review"
check "public: B sees both"         "$(login $B)$(open_l1 $B)2\n"        "^$C \("
check "set visibility shared"       "$(login $A)$(open_l1 $A)7\n2\n"     "L1 is now shared"
check "shared: B sees own"          "$(login $B)$(open_l1 $B)2\n"        "^$B \("
absent "shared: B can't see C's"    "$(login $B)$(open_l1 $B)2\n"        "^$C \("
check "creator sees both"           "$(login $A)$(open_l1 $A)2\n"        "^$C \("
check "B deletes: only own offered" "$(login $B)$(open_l1 $B)4\n2\n"     "Enter a whole number from 0 to 1"
check "delete review: no"           "$(login $A)$(open_l1 $A)4\n2\nn\n2\n" "^$C \\("
check "creator deletes C's"         "$(login $A)$(open_l1 $A)4\n2\ny\n"  "Deleted review by $C"

echo "== Unfriend unshares, delete list"
check "A removes B"                 "$(login $A)3\n5\n1\n"               "Removed $B"
sql   "only B's share row deleted"  "SELECT string_agg(sw.username, ',') FROM shared_with sw JOIN lists l ON l.id=sw.list_id WHERE l.creator='$A'" "$C"
check "B on cooldown"               "$(login $B)3\n4\n$A\n"              "another request in"
absent "B no longer sees L1"        "$(login $B)4\n1\n"                  "L1 \\["
check "My reviews: none"            "$(login $A)4\n4\n"                  "haven't written any reviews"
check "My reviews: lost access, still deletable" "$(login $B)4\n4\n1\ny\n" "Deleted your review"
sql   "ex-friend's review gone"     "SELECT count(*) FROM reviews WHERE username='$B'" "0"
sql   "trigger: ex-friend can't review" "INSERT INTO reviews (list_id, username, content) SELECT id, '$B', 'x' FROM lists WHERE creator='$A'" "ERROR:  You can only review lists you can see
CONTEXT:  PL/pgSQL function check_review_access() line 4 at RAISE"
check "delete list: no"             "$(login $A)$(open_l1 $A)10\nn\n1\n" "L1 \["
check "delete list: YES, leaves menu" "$(login $A)$(open_l1 $A)10\nYES\n1\n" "Deleted L1"
sql   "cascade cleaned up"          "SELECT (SELECT count(*) FROM lists WHERE creator='$A') + (SELECT count(*) FROM reviews WHERE username IN ('$B','$C'))" "0"

echo
echo "passed $pass, failed $fail"
[ $fail -eq 0 ]
