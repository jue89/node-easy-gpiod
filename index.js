const assert = require('assert');
const fs = require('fs');
const ll = require('./build/Release/gpiod_ll.node');
const EventEmitter = require('events');

function setBit (obj, key, no, val = true) {
	obj[key] = obj[key] || 0;
	if (val) obj[key] = (obj[key] | (1 << no)) >>> 0;
}

class AttrSet {
	constructor () {
		this.attrs = [];
	}

	update (match = {}, update = () => {}) {
		let attr = this.attrs.find((cand) => Object.entries(match).reduce((found, [key, value]) => found && cand[key] === value, true));
		if (!attr) {
			attr = match;
			assert(this.attrs.length < 10, 'Attrs overflow');
			this.attrs.push(attr);
		}
		update(attr);
	}
}

function bias (bias) {
	if (!bias || bias === 'none') return 0;
	if (bias === 'pull-up') return 1;
	if (bias === 'pull-down') return 2;
	throw new Error('Options bias must be: none | pull-up | pull-down');
}

function Input (pin, cfg = {}) {
	return {
		output: false,
		pin,
		active_low: !!cfg.active_low,
		bias: bias(cfg.bias),
		rising_edge: !!cfg.rising_edge,
		falling_edge: !!cfg.falling_edge,
		debounce: cfg.debounce,
	};
}

function drive (drive) {
	if (!drive || drive === 'push-pull') return 0;
	if (drive === 'open-drain') return 1;
	if (drive === 'open-source') return 2;
	throw new Error('Options drive must be: push-pull | open-drain | open-source');
}

function Output (pin, cfg = {}) {
	return {
		output: true,
		pin,
		active_low: !!cfg.active_low,
		drive: drive(cfg.drive),
		initial_value: cfg.initial_value,
		final_value: cfg.final_value,
	};
}

function openGpioChip (path) {
	const chip_fd = fs.openSync(path, 'r');

	let info;
	let lines;

	class GpioChip {
		constructor () {
			this.requests = [];
		}

		get info () {
			if (!info) {
				info = ll.GetChipInfo(chip_fd);
			}

			return info;
		}

		get lines () {
			if (!lines) {
				lines = [];
				for (let i = 0; i < this.info.line_cnt; i++) {
					lines.push(ll.GetLineInfo(chip_fd, i));
				}
			}

			return lines;
		}

		requestLines (consumer = '', lines = {}) {
			const attrSet = new AttrSet();
			const offsets = Object.entries(lines).map(([name, l], n) => {
				l.idx = n;
				l.name = name;
				l.offset = l.pin;
				if (typeof l.offset !== 'number') {
					const idx = this.lines.findIndex(({name}) => name === l.offset);
					assert(idx >= 0, `Cannot find line with name ${l.offset}`);
					l.offset = idx;
				}
				assert(l.offset >= 0 && l.offset < this.info.line_cnt, `Invalid line number: ${l.offset}`);

				if (l.output) {
					if (l.initial_value !== undefined) {
						attrSet.update({type: 2}, (x) => {
							setBit(x, 'mask', n);
							setBit(x, 'values', n, l.initial_value);
						});
					}

					attrSet.update(
						{type: 1, active_low: l.active_low, output: true, drive: l.drive},
						(x) => setBit(x, 'mask', n)
					);
				} else {
					if (l.debounce) {
						attrSet.update({type: 3, debounce: l.debounce}, (x) => setBit(x, 'mask', n));
					}

					attrSet.update(
						{type: 1, active_low: l.active_low, output: false, bias: l.bias, rising_edge: l.rising_edge, falling_edge: l.falling_edge},
						(x) => setBit(x, 'mask', n)
					);
				}

				return l.offset;
			});

			const {fd, release} = ll.RequestLines(chip_fd, consumer, offsets, attrSet.attrs, edgeEvent);

			const self = this;
			class Request extends EventEmitter {
				constructor (lines) {
					super();
					this.lines = lines;
				}

				close () {
					if (this.closed) return;
					this.closed = true;

					Object.values(this.lines).forEach((l) => {
						if (l.final_value !== undefined) {
							l.value = l.final_value;
						}
					});
					self.requests = self.requests.filter((r) => r !== this);
					release();
				}
			}

			class Line extends EventEmitter {
				constructor ({name, idx, offset}) {
					super();
					this.idx = idx;
					this.name = name;
					this.offset = offset;
				}

				get value () {
					const mask = (1 << this.idx) >>> 0;
					const bits = ll.GetValues(fd, mask);
					return bits > 0;
				}
			}

			class InputLine extends Line {
				constructor (info) {
					super(info);
				}
			}

			class OutputLine extends Line {
				constructor (info) {
					super(info);
					this.final_value = info.final_value;
				}

				get value () {
					return super.value;
				}

				set value (v) {
					const mask = (1 << this.idx) >>> 0;
					const bits = v ? mask : 0;
					ll.SetValues(fd, mask, bits);
				}
			}

			const req = new Request(Object.fromEntries(Object.entries(lines).map(([name, l]) => {
				const line = l.output ? new OutputLine(l) : new InputLine(l);
				return [name, line];
			})));

			function edgeEvent ({offset, rising_edge}) {
				const line = Object.values(req.line).find((l) => l.offset === offset);
				line.emit('change', rising_edge);
				req.emit('change', line, rising_edge);
			}

			this.requests.push(req);
			return req;
		}

		close () {
			if (this.closed) return;
			this.closed = true;

			this.requests.forEach((req) => req.close());
			fs.close(chip_fd);
		}
	}

	const chip = new GpioChip();
	process.on('exit', () => chip.close());

	return chip;
}

function assertLinesGetInputOnClose (chip_path, pin) {
	let chip = openGpioChip(chip_path);
	if (typeof pin === 'string') {
		pin = chip.lines.findIndex(({name}) => name === pin);
		assert(pin >= 0);
	}

	assert(!chip.lines[pin].flags.used, `Line ${pin} is in use!`);
	assert(chip.lines[pin].flags.input, `Line ${pin} is not an input!`);

	// Make line output
	chip.requestLines('output-test', {out: Output(pin)});

	// Close chip
	chip.close();

	// Reopen chip
	chip = openGpioChip(chip_path);

	// Check input state
	assert(chip.lines[pin].flags.input, `Line ${pin} still is output after close!`);
}

module.exports = {openGpioChip, Input, Output, assertLinesGetInputOnClose};
