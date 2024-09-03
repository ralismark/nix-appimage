{ runCommandCC
}:

runCommandCC "AppRun" { } ''
  mkdir $out
  mkdir $out/mountroot
  $CC ${./main.c} -o $out/AppRun
''
