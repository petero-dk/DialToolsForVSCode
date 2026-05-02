// Patches node-gyp's Visual Studio finder to recognise VS 2026 (version 18).
// node-gyp only knows versions 15–17 (VS 2017–2022); newer releases fail with
// "unknown version". We map version 18 to the same year-string as 17 so the
// existing MSBuild lookup path is reused.
//
// This script is idempotent — running it multiple times is safe.

const fs   = require('fs');
const path = require('path');

const target = path.join(__dirname, '..', 'node_modules', 'node-gyp',
                         'lib', 'find-visualstudio.js');

if (!fs.existsSync(target)) {
    console.log('patch-node-gyp: node-gyp not found, skipping.');
    process.exit(0);
}

const original = fs.readFileSync(target, 'utf8');

const needle = `    if (ret.versionMajor === 17) {
      ret.versionYear = 2022
      return ret
    }`;

const patched = `    if (ret.versionMajor === 17) {
      ret.versionYear = 2022
      return ret
    }
    if (ret.versionMajor === 18) {
      ret.versionYear = 2022
      return ret
    }`;

if (original.includes(patched)) {
    console.log('patch-node-gyp: already applied, nothing to do.');
    process.exit(0);
}

if (!original.includes(needle)) {
    console.warn('patch-node-gyp: expected code not found — node-gyp may have changed. Skipping.');
    process.exit(0);
}

fs.writeFileSync(target, original.replace(needle, patched), 'utf8');
console.log('patch-node-gyp: patched find-visualstudio.js to support VS 2026 (version 18).');
