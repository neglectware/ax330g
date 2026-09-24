/* Single source of truth for the AX330G plugin's version. CMakeLists.txt
 * reads both macros (file(STRINGS...) + regex) so CFBundleShortVersionString
 * (AX_VERSION) and CFBundleVersion (AX_VERSION.AX_BUILD), the AU component
 * version, and the .pkg version all agree and installers upgrade cleanly.
 * Bump AX_BUILD by one on every build. Bump AX_VERSION's minor for a new
 * block or a sound-changing model change, the patch for fixes. Same
 * build-number convention as the author's other apps, plus a semantic
 * version because the plugin ships to other people. */
#ifndef AX_VERSION_H
#define AX_VERSION_H
#define AX_VERSION "0.12.0"
#define AX_BUILD 25
#endif
