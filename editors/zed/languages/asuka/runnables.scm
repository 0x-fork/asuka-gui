; test "name" { … }: run that test.
((test_declaration
  "test" @run
  name: (string_literal
    (string_content) @ASUKA_TEST_NAME))
  (#set! tag asuka-test))

; bench "name" { … }: time that bench.
((bench_declaration
  "bench" @run
  name: (string_literal
    (string_content) @ASUKA_BENCH_NAME))
  (#set! tag asuka-bench))

; app { … }: run the app.
((app_declaration
  "app" @run)
  (#set! tag asuka-app))
