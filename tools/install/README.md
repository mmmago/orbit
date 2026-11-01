# Installing Orbit on a Mac

1. Package: `tools/filedrag/package-macos.sh build ../dist-orbit "<codesign identity>"`
   (this empties `../dist-orbit` first).
2. `install-orbit.sh <Orbit.zip> <sha256 of Orbit.app/Contents/MacOS/orbit-core>`
   puts Orbit.app in /Applications and, the first time, copies the Deskflow
   settings and certificates from ~/Library/Deskflow to ~/Library/Orbit.
   Quit Orbit first when updating it.
3. `switch-to-orbit.sh` (once, when replacing Deskflow): quits Deskflow,
   moves it to ~/Deskflow-backups and starts Orbit.
4. `update-orbit.sh <Orbit.zip> <sha256>`: quit Orbit, install a new build, start it again.
