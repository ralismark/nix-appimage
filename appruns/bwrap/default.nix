{ runCommand
}:

runCommand "AppRun" { } ''
  mkdir $out
  cp ${./test.sh} $out/AppRun
''
