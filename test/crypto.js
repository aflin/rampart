var crypto = require("rpcrypto");
var buffer = crypto.encrypt({
  key: "01234567890123456789012345678901",
  iv: "0123456789012345",
  cipher: "aes-256-cbc",
  buffer: new TextEncoder().encode("hello world"),
});
console.log(buffer.length);
