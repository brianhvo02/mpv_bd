# mpv (Bluray Edition)
A Qt app with mpv embedded used with libbluray to play Bluray discs.

## Build Process
```bash
qmake6 -o build/Makefile
make -C build
build/mpv_bd
```