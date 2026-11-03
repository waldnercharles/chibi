'use strict';
const load = require(process.argv[2]);
load().then(module => {
    const result = module._clock_test();
    if (result !== 0) throw new Error(`clock proof returned ${result}`);
});
