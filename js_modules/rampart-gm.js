"noTranspile";

/* rampart-graphicsmagick requires a system GraphicsMagick install
   (libGraphicsMagickWand) on PATH for the OS's dynamic linker.  We do
   NOT bundle GraphicsMagick or any of its codec dependencies, because
   the typical brew/apt/pkg builds of GraphicsMagick include libheif
   compiled with libx265 (GPL-2-or-later) -- redistributing that
   alongside the proprietary rampart-sql in the same project would
   violate GPL.  See claude-work/rampart-gm-bundling-backup/ for the
   bundling code we removed, in case the GPL situation changes. */

try {
    module.exports = require("rampart-graphicsmagick");
} catch (e) {
    var platform = (rampart.buildPlatform || "").toLowerCase();
    var install = "";
    if (/darwin|macos/.test(platform)) {
        install =
            "  brew install graphicsmagick\n" +
            "    (if Homebrew is not installed, see https://brew.sh)\n";
    } else if (/freebsd/.test(platform)) {
        install =
            "  sudo pkg install graphicsmagick\n";
    } else if (/debian|ubuntu|raspberry|raspbian/.test(platform)) {
        install =
            "  sudo apt install libgraphicsmagick3 libgraphicsmagickwand-q16-2\n";
    } else if (/fedora|rhel|centos|rocky|alma/.test(platform)) {
        install =
            "  sudo dnf install GraphicsMagick\n";
    } else if (/alpine/.test(platform)) {
        install =
            "  sudo apk add graphicsmagick\n";
    } else {
        install =
            "  macOS    : brew install graphicsmagick\n" +
            "  Debian   : sudo apt install libgraphicsmagick3 libgraphicsmagickwand-q16-2\n" +
            "  Fedora   : sudo dnf install GraphicsMagick\n" +
            "  FreeBSD  : sudo pkg install graphicsmagick\n" +
            "  Alpine   : sudo apk add graphicsmagick\n";
    }
    throw new Error(
        e.message + "\n\n" +
        "rampart-graphicsmagick requires GraphicsMagick to be installed\n" +
        "on the system.  Install with your platform's package manager:\n\n" +
        install + "\n" +
        "Documentation: https://rampart.dev/docs/rampart-gm.html\n"
    );
}
