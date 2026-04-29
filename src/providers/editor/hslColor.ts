/**
 * HSL Color utility class.
 * Ported from https://richnewman.wordpress.com/about/code-listings-and-diagrams/hslcolor-class/
 */
export class HSLColor {
    // Private data members are on scale 0-1.
    // They are scaled for use externally based on scale.
    private _hue = 1.0;
    private _saturation = 1.0;
    private _luminosity = 1.0;

    private static readonly SCALE = 240.0;

    get hue(): number {
        return this._hue * HSLColor.SCALE;
    }
    set hue(value: number) {
        this._hue = HSLColor.checkRange(value / HSLColor.SCALE);
    }

    get saturation(): number {
        return this._saturation * HSLColor.SCALE;
    }
    set saturation(value: number) {
        this._saturation = HSLColor.checkRange(value / HSLColor.SCALE);
    }

    get luminosity(): number {
        return this._luminosity * HSLColor.SCALE;
    }
    set luminosity(value: number) {
        this._luminosity = HSLColor.checkRange(value / HSLColor.SCALE);
    }

    private static checkRange(value: number): number {
        if (value < 0.0) {
            return 0.0;
        }
        if (value > 1.0) {
            return 1.0;
        }
        return value;
    }

    /** Convert to RGB tuple [r, g, b] each 0-255. */
    toRGB(): [number, number, number] {
        let r = 0, g = 0, b = 0;
        if (this._luminosity !== 0) {
            if (this._saturation === 0) {
                r = g = b = this._luminosity;
            } else {
                const temp2 = this.getTemp2();
                const temp1 = 2.0 * this._luminosity - temp2;

                r = HSLColor.getColorComponent(temp1, temp2, this._hue + 1.0 / 3.0);
                g = HSLColor.getColorComponent(temp1, temp2, this._hue);
                b = HSLColor.getColorComponent(temp1, temp2, this._hue - 1.0 / 3.0);
            }
        }
        return [Math.round(255 * r), Math.round(255 * g), Math.round(255 * b)];
    }

    private static getColorComponent(temp1: number, temp2: number, temp3: number): number {
        temp3 = HSLColor.moveIntoRange(temp3);
        if (temp3 < 1.0 / 6.0) {
            return temp1 + (temp2 - temp1) * 6.0 * temp3;
        } else if (temp3 < 0.5) {
            return temp2;
        } else if (temp3 < 2.0 / 3.0) {
            return temp1 + (temp2 - temp1) * ((2.0 / 3.0) - temp3) * 6.0;
        }
        return temp1;
    }

    private static moveIntoRange(temp3: number): number {
        if (temp3 < 0.0) {
            return temp3 + 1.0;
        }
        if (temp3 > 1.0) {
            return temp3 - 1.0;
        }
        return temp3;
    }

    private getTemp2(): number {
        if (this._luminosity < 0.5) {
            return this._luminosity * (1.0 + this._saturation);
        }
        return this._luminosity + this._saturation - this._luminosity * this._saturation;
    }

    /** Create an HSLColor from RGB components (each 0-255). */
    static fromRGB(r: number, g: number, b: number): HSLColor {
        const rNorm = r / 255;
        const gNorm = g / 255;
        const bNorm = b / 255;

        const max = Math.max(rNorm, gNorm, bNorm);
        const min = Math.min(rNorm, gNorm, bNorm);
        const delta = max - min;

        const luminosity = (max + min) / 2;
        let hue = 0;
        let saturation = 0;

        if (delta !== 0) {
            saturation = luminosity < 0.5
                ? delta / (max + min)
                : delta / (2 - max - min);

            if (max === rNorm) {
                hue = (gNorm - bNorm) / delta + (gNorm < bNorm ? 6 : 0);
            } else if (max === gNorm) {
                hue = (bNorm - rNorm) / delta + 2;
            } else {
                hue = (rNorm - gNorm) / delta + 4;
            }
            hue /= 6;
        }

        const color = new HSLColor();
        color._hue = hue;
        color._saturation = saturation;
        color._luminosity = luminosity;
        return color;
    }

    /** Create an HSLColor from a hex color string (e.g. "#ff0000" or "#f00"). */
    static fromHex(hex: string): HSLColor {
        let h = hex.startsWith('#') ? hex.slice(1) : hex;
        if (h.length === 3) {
            h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
        }
        const r = parseInt(h.substring(0, 2), 16);
        const g = parseInt(h.substring(2, 4), 16);
        const b = parseInt(h.substring(4, 6), 16);
        return HSLColor.fromRGB(r, g, b);
    }
}
