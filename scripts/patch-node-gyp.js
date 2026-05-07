// Patches node-gyp's Visual Studio finder to fully support VS 2026 (version 18).
// node-gyp only knows versions 15–17 (VS 2017–2022). The following places all
// need updating so that VS 2026 is detected and used:
//
//   1. getVersionInfo       – map versionMajor 18 → versionYear 2026
//   2. findVisualStudio2019OrNewerFromSpecifiedLocation
//   3. findVisualStudio2019OrNewerUsingSetupModule
//   4. findVisualStudio2019OrNewer
//      (all three pass a hard-coded [2019, 2022] allowlist to processData;
//       2026 must be added so VS 2026 is not filtered out as "unsupported")
//   5. getToolset           – return 'v180' for versionYear 2026
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

let src = fs.readFileSync(target, 'utf8');
let changed = false;

// Helper: apply one replacement if the needle is present and the result is not
// already in the file.
function applyPatch(needle, replacement, label) {
    if (src.includes(replacement)) {
        console.log(`patch-node-gyp: ${label} — already applied.`);
        return;
    }
    if (!src.includes(needle)) {
        console.warn(`patch-node-gyp: ${label} — expected code not found, skipping (node-gyp may have changed).`);
        return;
    }
    src = src.replace(needle, replacement);
    changed = true;
    console.log(`patch-node-gyp: ${label} — applied.`);
}

// 1. getVersionInfo: map versionMajor 18 → versionYear 2026
applyPatch(
    `    if (ret.versionMajor === 17) {
      ret.versionYear = 2022
      return ret
    }`,
    `    if (ret.versionMajor === 17) {
      ret.versionYear = 2022
      return ret
    }
    if (ret.versionMajor === 18) {
      ret.versionYear = 2026
      return ret
    }`,
    'getVersionInfo VS 2026'
);

// 2. findVisualStudio2019OrNewerFromSpecifiedLocation: add 2026 to allowed years
applyPatch(
    `async findVisualStudio2019OrNewerFromSpecifiedLocation () {
    return this.findVSFromSpecifiedLocation([2019, 2022])
  }`,
    `async findVisualStudio2019OrNewerFromSpecifiedLocation () {
    return this.findVSFromSpecifiedLocation([2019, 2022, 2026])
  }`,
    'findVisualStudio2019OrNewerFromSpecifiedLocation VS 2026'
);

// 3. findVisualStudio2019OrNewerUsingSetupModule: add 2026 to allowed years
applyPatch(
    `async findVisualStudio2019OrNewerUsingSetupModule () {
    return this.findNewVSUsingSetupModule([2019, 2022])
  }`,
    `async findVisualStudio2019OrNewerUsingSetupModule () {
    return this.findNewVSUsingSetupModule([2019, 2022, 2026])
  }`,
    'findVisualStudio2019OrNewerUsingSetupModule VS 2026'
);

// 4. findVisualStudio2019OrNewer: add 2026 to allowed years
applyPatch(
    `async findVisualStudio2019OrNewer () {
    return this.findNewVS([2019, 2022])
  }`,
    `async findVisualStudio2019OrNewer () {
    return this.findNewVS([2019, 2022, 2026])
  }`,
    'findVisualStudio2019OrNewer VS 2026'
);

// 5. getToolset: return 'v180' for VS 2026.
//    VS 2026 uses MSVC 14.40+ whose MSBuild platform toolset identifier is v180.
applyPatch(
    `    } else if (versionYear === 2022) {
      return 'v143'
    }
    this.log.silly('- invalid versionYear:', versionYear)`,
    `    } else if (versionYear === 2022) {
      return 'v143'
    } else if (versionYear === 2026) {
      return 'v180'
    }
    this.log.silly('- invalid versionYear:', versionYear)`,
    'getToolset VS 2026'
);

if (changed) {
    fs.writeFileSync(target, src, 'utf8');
    console.log('patch-node-gyp: find-visualstudio.js updated.');
} else {
    console.log('patch-node-gyp: nothing to do.');
}
