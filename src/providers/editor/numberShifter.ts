/**
 * Shifts a numeric string value up or down.
 * @param text - The numeric string (e.g. "42", "3.14", ".5").
 * @param direction - 1 to increase, -1 to decrease.
 * @returns The new numeric string, or null if the text is not a valid number.
 */
export function shiftNumber(text: string, direction: 1 | -1): string | null {
    const number = parseFloat(text);
    if (isNaN(number)) {
        return null;
    }

    const delta = getDelta(text);
    const newValue = number + direction * delta;

    const decimalPlaces = getDecimalPlaces(text);
    if (decimalPlaces === 0) {
        return Math.round(newValue).toString();
    } else if (decimalPlaces === 1) {
        return newValue.toFixed(1);
    } else {
        // More than one decimal place – use the same format as the original
        return newValue.toFixed(decimalPlaces).replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '');
    }
}

function getDelta(value: string): number {
    const decimals = getDecimalPlaces(value);
    if (decimals > 1) {
        return 0.01;
    } else if (decimals === 1) {
        return 0.1;
    }
    return 1;
}

function getDecimalPlaces(value: string): number {
    const dotIndex = value.indexOf('.');
    if (dotIndex < 0) {
        return 0;
    }
    return value.length - dotIndex - 1;
}
