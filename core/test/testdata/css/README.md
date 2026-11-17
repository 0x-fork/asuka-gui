The core's layout (core/src/layout) against Chrome.

The fixtures in the directories come from Taffy
(https://github.com/DioxusLabs/taffy, MIT, see LICENSE-taffy): each is an
HTML test laid out by Chrome, with Chrome's results as the expectations.
Only the border-box, left-to-right variants are kept. ltr_chrome.xml and
rtl_chrome.xml are the cases beyond them, rendered by Chrome as each says.

gen_fixtures.go parses them all into ../layout_fixtures.txt, which the C
test reads (core/test/layout_test.c); known_failures.txt lists the ones
the layout doesn't match yet.

    go run core/test/testdata/css/gen_fixtures.go      # after changing a fixture
    go test ./core -run TestC/Layout                   # the C test
    go test ./core -run TestC/Layout -update-known     # after fixing layout: rewrite known_failures.txt
