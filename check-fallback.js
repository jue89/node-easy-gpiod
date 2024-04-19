const assert = require('assert');

const {assertLinesGetInputOnClose} = require('.');

assert(process.argv.length === 4, 'Parameter missing! Make sure to state [chip] [pin]');

const chip = process.argv[2];
const pin = parseInt(process.argv[3]);

assertLinesGetInputOnClose(chip, pin);

console.log('Lines fall back into input mode after close - everything looks sane :)');
