var test = require("./test");
try {
  require("bogus");
  test.assert(false, "require('bogus') did not throw");
} catch (exception) {}
