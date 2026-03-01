"use transpilerGlobally"
import defPayload, {
  f,
  aa, bee,
  q, r, e, u,
  rest,
  x, y, tail
} from "./export-module.js";

import * as M from "./export-module.js";
import { aa as AA, bee as Bee } from "./export-module.js";
import dp from "./export-module.js";

var obj2,x1 = 1,y1 = 1;
eval("obj2={ x1, y1 }");
console.log("result:", JSON.stringify(obj2));
