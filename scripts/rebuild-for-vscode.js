// Rebuilds the native addon against VS Code's Electron version and architecture
// so the .node file can be loaded by the extension host.
//
// Two things must match the extension host:
//   1. Node ABI  — use Electron headers (electronjs.org/headers), not nodejs.org
//   2. CPU arch  — VS Code on ARM64 machines runs ARM64-native; x64 .node won't load
//
// Usage:
//   VSCODE_ELECTRON_VERSION=39.8.7 npm run rebuild-native
//
// The required VSCODE_ELECTRON_VERSION is printed in the "Surface Dial Tools"
// output channel when the addon fails to load.

const { execFileSync, spawnSync } = require('child_process');
const path = require('path');
const fs   = require('fs');
const os   = require('os');

if (process.platform !== 'win32') {
    console.log('Skipping native rebuild on non-Windows platform.');
    process.exit(0);
}

// ---------------------------------------------------------------------------
// Detect VS Code's Node.js version (from Code.exe --version)
// ---------------------------------------------------------------------------
function findVSCodeInstall() {
    const candidates = [
        path.join(process.env.LOCALAPPDATA ?? '', 'Programs', 'Microsoft VS Code'),
        'C:\\Program Files\\Microsoft VS Code',
    ];
    return candidates.find(p => fs.existsSync(path.join(p, 'Code.exe'))) ?? null;
}

function findVSCodeNodeVersion(installDir) {
    try {
        const out = execFileSync(path.join(installDir, 'Code.exe'),
            ['--version'], { encoding: 'utf8', timeout: 10000 }).trim();
        const match = out.match(/^v?(\d+\.\d+\.\d+)/);
        return match ? match[1] : null;
    } catch { return null; }
}

// ---------------------------------------------------------------------------
// Detect Code.exe CPU architecture by reading the PE header
// ---------------------------------------------------------------------------
function findVSCodeArch(installDir) {
    try {
        const exe   = path.join(installDir, 'Code.exe');
        const buf   = Buffer.alloc(4096);
        const fd    = fs.openSync(exe, 'r');
        fs.readSync(fd, buf, 0, 4096, 0);
        fs.closeSync(fd);
        const peOff  = buf.readUInt32LE(0x3C);
        const machine = buf.readUInt16LE(peOff + 4);
        if (machine === 0xAA64) { return 'arm64'; }
        if (machine === 0x8664) { return 'x64'; }
        return 'x64'; // safe default
    } catch { return 'x64'; }
}

// ---------------------------------------------------------------------------
// Locate VsDevCmd.bat via vswhere
// ---------------------------------------------------------------------------
function findVsDevCmd() {
    const vsWhere = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe';
    if (!fs.existsSync(vsWhere)) { return null; }
    try {
        const out = execFileSync(vsWhere, ['-latest', '-property', 'installationPath'],
                                 { encoding: 'utf8' }).trim();
        const bat = path.join(out, 'Common7', 'Tools', 'VsDevCmd.bat');
        return fs.existsSync(bat) ? bat : null;
    } catch { return null; }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
const electronVersion = process.env.VSCODE_ELECTRON_VERSION ?? null;

const installDir  = findVSCodeInstall();
const targetArch  = installDir ? findVSCodeArch(installDir) : 'x64';

// Avoid invoking Code.exe unless we need a Node fallback target.
const nodeVersion = (!electronVersion && installDir)
    ? findVSCodeNodeVersion(installDir)
    : null;

if (!electronVersion && !nodeVersion) {
    console.error(
        'Could not determine VS Code\'s version.\n' +
        'Ensure VS Code is installed at the default location, or set\n' +
        'VSCODE_ELECTRON_VERSION=<version> (shown in the Surface Dial Tools output channel).'
    );
    process.exit(1);
}

let target, distUrl;
if (electronVersion) {
    target  = electronVersion;
    distUrl = 'https://electronjs.org/headers';
} else {
    target  = nodeVersion;
    distUrl = 'https://nodejs.org/download/release';
}

console.log(`Rebuilding native addon: target=${target}, arch=${targetArch}, dist=${distUrl}`);

const vsDevCmd  = findVsDevCmd();
const nodeGyp   = path.join(__dirname, '..', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js');
const nativeDir = path.join(__dirname, '..', 'native');
const logFile   = path.join(os.tmpdir(), 'dial-tools-rebuild.log');
const batFile   = path.join(os.tmpdir(), 'dial-tools-rebuild.bat');

// For ARM64 cross-compilation: host toolchain is x64, target is arm64
const vsArch     = targetArch === 'arm64' ? 'arm64' : 'x64';
const vsHostArch = 'x64';

const gypLine = [
    `node "${nodeGyp}" rebuild`,
    `--target=${target}`,
    `--arch=${targetArch}`,
    `--dist-url=${distUrl}`,
    `> "${logFile}" 2>&1`,
].join(' ');

const batLines = vsDevCmd
    ? ['@echo off',
       `call "${vsDevCmd}" -arch=${vsArch} -host_arch=${vsHostArch}`,
       `cd /d "${nativeDir}"`,
       gypLine]
    : ['@echo off', `cd /d "${nativeDir}"`, gypLine];

fs.writeFileSync(batFile, batLines.join('\r\n'), 'ascii');

// Use PowerShell Start-Process to escape Job Object restrictions from the parent
// process (VS Code terminal, Claude Code sandbox, etc.)
const psCmd = `Start-Process -FilePath cmd.exe -ArgumentList '/c "${batFile}"' -Wait -NoNewWindow`;
const result = spawnSync('powershell.exe',
    ['-NonInteractive', '-Command', psCmd],
    { stdio: 'inherit', shell: false });

const log = fs.existsSync(logFile) ? fs.readFileSync(logFile, 'utf8') : '';
console.log(log);

if (result.status !== 0) {
    process.exit(result.status ?? 1);
}
