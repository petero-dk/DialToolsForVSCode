// Builds the native addon by launching node-gyp inside a VS Developer
// Command Prompt, which is required on Windows for MSVC to be on PATH.
// Spawns cmd.exe with START /WAIT so it escapes any Job Object restrictions
// imposed by the parent process (e.g. VS Code terminal).
//
// Set TARGET_ARCH env var to 'arm64' for cross-compilation from an x64 host.

const { execFileSync, spawnSync } = require('child_process');
const path = require('path');
const fs   = require('fs');
const os   = require('os');

if (process.platform !== 'win32') {
    console.log('Skipping native build on non-Windows platform.');
    process.exit(0);
}

const rebuild    = process.argv[2] === 'rebuild';
const targetArch = process.env.TARGET_ARCH || process.arch;
const hostArch   = process.arch === 'arm64' ? 'arm64' : 'x64';
const archArg    = `--arch=${targetArch}`;

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

function findVsMajorVersion() {
    const vsWhere = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe';
    if (!fs.existsSync(vsWhere)) { return null; }
    try {
        const out = execFileSync(vsWhere, ['-latest', '-property', 'installationPath'],
                                 { encoding: 'utf8' }).trim();
        const match = out.match(/\\(\d+)\\[^\\]+$/);
        return match ? Number(match[1]) : null;
    } catch {
        return null;
    }
}

function toolsetForVsMajor(major) {
    if (major >= 18) {
        return 'v180';
    }
    return null;
}

const vsDevCmd = findVsDevCmd();
const nodeGyp  = path.join(__dirname, '..', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js');
const retarget = path.join(__dirname, 'retarget-vcxproj-toolset.js');
const nativeDir = path.join(__dirname, '..', 'native');
const logFile   = path.join(os.tmpdir(), 'dial-tools-native-build.log');
const toolset   = toolsetForVsMajor(findVsMajorVersion());

const gypCleanLine = `node "${nodeGyp}" clean > "${logFile}" 2>&1`;
const gypConfigureLine = `node "${nodeGyp}" configure ${archArg} > "${logFile}" 2>&1`;
const gypBuildLine = `node "${nodeGyp}" build > "${logFile}" 2>&1`;
const retargetLine = toolset
    ? `node "${retarget}" ${toolset} >> "${logFile}" 2>&1`
    : null;

let batContent;

if (vsDevCmd) {
    const lines = [
        `@echo off`,
        `call "${vsDevCmd}" -arch=${targetArch} -host_arch=${hostArch}`,
        `cd /d "${nativeDir}"`,
    ];
    if (rebuild) {
        lines.push(gypCleanLine);
        lines.push(`if errorlevel 1 exit /b %errorlevel%`);
    }
    lines.push(gypConfigureLine);
    lines.push(`if errorlevel 1 exit /b %errorlevel%`);
    if (retargetLine) {
        lines.push(retargetLine);
        lines.push(`if errorlevel 1 exit /b %errorlevel%`);
    }
    lines.push(gypBuildLine);
    batContent = lines.join('\r\n');
} else {
    const lines = [
        `@echo off`,
        `cd /d "${nativeDir}"`,
    ];
    if (rebuild) {
        lines.push(gypCleanLine);
        lines.push(`if errorlevel 1 exit /b %errorlevel%`);
    }
    lines.push(gypConfigureLine);
    lines.push(`if errorlevel 1 exit /b %errorlevel%`);
    if (retargetLine) {
        lines.push(retargetLine);
        lines.push(`if errorlevel 1 exit /b %errorlevel%`);
    }
    lines.push(gypBuildLine);
    batContent = lines.join('\r\n');
}

const batFile = path.join(os.tmpdir(), 'dial-tools-build.bat');
fs.writeFileSync(batFile, batContent, 'ascii');

const psCmd = `$p = Start-Process -FilePath cmd.exe -ArgumentList '/c "${batFile}"' -Wait -NoNewWindow -PassThru; exit $p.ExitCode`;
const result = spawnSync('powershell.exe',
    ['-NonInteractive', '-Command', psCmd],
    { stdio: 'inherit', shell: false });

const log = fs.existsSync(logFile) ? fs.readFileSync(logFile, 'utf8') : '';
console.log(log);

if (result.status !== 0) {
    process.exit(result.status ?? 1);
}

const nodePath = path.join(nativeDir, 'build', 'Release', 'radial_controller.node');
if (!fs.existsSync(nodePath)) {
    console.error('Build completed but radial_controller.node was not produced.');
    process.exit(1);
}
