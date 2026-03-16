try {
    module.exports = require("rampart-graphicsmagick");
} catch (e) {
    throw new Error (e.message + "  Are libraries installed? See https://rampart.dev/docs/rampart-gm.html");
}
