// Builds the native addon by launching node-gyp inside a VS Developer
// Command Prompt, which is required on Windows for MSVC to be on PATH.
// Spawns cmd.exe with START /WAIT so it escapes any Job Object restrictions
// imposed by the parent process (e.g. VS Code terminal).

const { execFileSync, spawnSync } = require('child_process');
const path = require('path');
const fs   = require('fs');
const os   = require('os');

if (process.platform !== 'win32') {
    console.log('Skipping native build on non-Windows platform.');
    process.exit(0);
}

const rebuild = process.argv[2] === 'rebuild';
const action  = rebuild ? 'rebuild' : 'configure build';

// Locate VsDevCmd.bat via vswhere
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

const vsDevCmd = findVsDevCmd();
const nodeGyp  = path.join(__dirname, '..', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js');
const nativeDir = path.join(__dirname, '..', 'native');
const logFile   = path.join(os.tmpdir(), 'dial-tools-native-build.log');

let batContent;
if (vsDevCmd) {
    batContent = [
        `@echo off`,
        `call "${vsDevCmd}" -arch=x64 -host_arch=x64`,
        `cd /d "${nativeDir}"`,
        `node "${nodeGyp}" ${action}`,
    ].join('\r\n');
} else {
    batContent = [
        `@echo off`,
        `cd /d "${nativeDir}"`,
        `node "${nodeGyp}" ${action}`,
    ].join('\r\n');
}

const batFile = path.join(os.tmpdir(), 'dial-tools-build.bat');
fs.writeFileSync(batFile, batContent, 'ascii');

const psCmd = `Start-Process -FilePath cmd.exe -ArgumentList '/c "${batFile}" > "${logFile}" 2>&1' -Wait -NoNewWindow`;
const result = spawnSync('powershell.exe',
    ['-NonInteractive', '-Command', psCmd],
    { stdio: 'inherit', shell: false });

const log = fs.existsSync(logFile) ? fs.readFileSync(logFile, 'utf8') : '';
console.log(log);

if (result.status !== 0) {
    process.exit(result.status ?? 1);
}
