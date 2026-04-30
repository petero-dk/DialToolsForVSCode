import { HSLColor } from './hslColor';

/**
 * Shifts a hex color string by adjusting its luminosity.
 * @param hex - The hex color string (e.g. "#ff0000" or "#f00").
 * @param direction - 1 to lighten, -1 to darken.
 * @returns The new hex color string in lowercase.
 */
export function shiftColor(hex: string, direction: 1 | -1): string {
    const hsl = HSLColor.fromHex(hex);
    const factor = direction === -1 ? -3 : 3;
    hsl.luminosity += factor;
    const [r, g, b] = hsl.toRGB();
    return '#' + toHex2(r) + toHex2(g) + toHex2(b);
}

function toHex2(value: number): string {
    return Math.max(0, Math.min(255, value)).toString(16).padStart(2, '0');
}
