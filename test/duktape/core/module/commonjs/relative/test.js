exports.assert = function (guard, message) {
  if (!guard) throw new Error(message + " FAILED");
};
