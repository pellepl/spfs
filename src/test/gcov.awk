BEGIN {
  test_pass = 1
  printf "\nTEST COVERAGE REPORT\n--------------------\n"
}

match($0, /File '(.*)'/, a) {
  covfile = a[1]
  ix = index(GCOV_FILES, covfile) 
  if (ix) {
    req_perc = strtonum(substr(GCOV_FILES, ix + length(covfile) + length(GCOV_ANNOTATION) + 1, 2))
    check = 1
  } else {
    check = 0
  }
}

match($0, /Lines executed:(.*)%/, a) {
  if (check) {
    perc = strtonum(a[1])
    if (perc <= req_perc) {
      test_pass = 0
      printf("\x1b[31;1mFAIL\x1b[0m   ")
    } else if (perc <= req_perc + (100-req_perc)/2) {
      printf("\x1b[33;1mPASS\x1b[0m   ")
    } else {
      printf("\x1b[32;1mGOOD\x1b[0m   ")
    }
    printf "%2d%%/%2d%%\t%s", perc, req_perc, covfile
    printf("\n")
  }
}

END {
  if (test_pass == 1)
    exit(0)
  else
    exit(1)
}
