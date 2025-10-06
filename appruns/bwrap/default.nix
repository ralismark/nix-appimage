{ runCommand,
  pkgsStatic
}:

runCommand "AppRun" { } ''
  mkdir $out
  cp ${./test.sh} $out/AppRun
  cp ${pkgsStatic.bubblewrap}/bin/bwrap $out/bwrap
''
