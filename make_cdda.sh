 ./tools/mkdcdisc \
  -c DETHRACE/MUSIC/Track02.wav -c DETHRACE/MUSIC/Track03.wav -c DETHRACE/MUSIC/Track04.wav -c DETHRACE/MUSIC/Track05.wav -c DETHRACE/MUSIC/Track06.wav -c DETHRACE/MUSIC/Track07.wav -c DETHRACE/MUSIC/Track08.wav -c DETHRACE/MUSIC/Track09.wav \
  -e dethrace.elf  \
  -d DETHRACE -o dethrace-dc.cdi -n "dethrace"

~/flycast-x86_64.AppImage dethrace-dc.cdi