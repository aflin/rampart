"noTranspile";

/* Tell GraphicsMagick where its .mgk config + codec/filter plugins
   live BEFORE we dlopen the native module.  rampart-graphicsmagick.c
   also calls setenv() in duk_open_module, which is enough on macOS 11
   (dyld 3 delays library constructors).  On macOS 12+ (dyld 4) and
   anywhere else that runs library constructors during dlopen,
   libGraphicsMagick reads MAGICK_CONFIGURE_PATH before duk_open_module
   ever fires -- so the env var has to be set up here, in pure JS,
   before the require() triggers the load.
   Guarded on the share/graphicsmagick directory actually existing;
   if a user has rampart installed without the rampart-graphicsmagick
   package (and thus no bundled .mgk config), this is a no-op and
   GM uses its compile-time path / a system install. */
if (process.installPath) {
    var _gm_share = process.installPath + "/share/graphicsmagick";
    var _gm_st;
    try { _gm_st = rampart.utils.stat(_gm_share); } catch (e) { _gm_st = null; }
    if (_gm_st && _gm_st.isDirectory) {
        /* overwrite=false so a caller-supplied env var still wins. */
        rampart.utils.setenv("MAGICK_CONFIGURE_PATH", _gm_share, false);
        var _gm_coders = _gm_share + "/modules-Q16/coders";
        try {
            if (rampart.utils.stat(_gm_coders))
                rampart.utils.setenv("MAGICK_CODER_MODULE_PATH", _gm_coders, false);
        } catch (e) {}
        var _gm_filters = _gm_share + "/modules-Q16/filters";
        try {
            if (rampart.utils.stat(_gm_filters))
                rampart.utils.setenv("MAGICK_FILTER_MODULE_PATH", _gm_filters, false);
        } catch (e) {}
    }
}

try {
    module.exports = require("rampart-graphicsmagick");
} catch (e) {
    throw new Error (e.message + "  Are libraries installed? See https://rampart.dev/docs/rampart-gm.html");
}
