# 🚨 Easy gpiod

## Example

```js
const {openGpioChip, Input, Output} = require('easy-gpiod');

const chip = openGpioChip('/dev/gpiochip0');

const {led, btn} = chip.requestLines('blinky', {
    // Use the 20th line as output
    led: Output(20, {
        value: false,         // Make sure to start with the LED turned off
        final_value: false,   // Turn off on exit to workaround gpio chips retaining output mode
    }),

    // Use the line with the name "GPIO21" as button input.
    // The button pulls the line to GND when pressed.
    btn: Input('GPIO21', {
        bias: 'pull-up',   // Turn on pull-up resistor
        low_active: true,  // The line is LOW if the button is pressed
        rising_edge: true, // Detect rising edges (it's inverted due to low_active setting)
        debounce: 100000   // Debounce the button with 100.000us
    })
}).lines;

btn.on('change', () => {
    // Toggle LED when the button is pressed
    led.value = !led.value;
});
```