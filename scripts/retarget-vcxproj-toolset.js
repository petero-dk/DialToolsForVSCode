const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

/**
 * Finds an available MSVC toolset on the system by checking the registry or
 * by parsing installed VS locations via vswhere.
 */
function findAvailableToolset() {
    const vsWhere = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe';
    
    try {
        if (!fs.existsSync(vsWhere)) {
            return null;
        }

        const installPath = execFileSync(vsWhere, ['-latest', '-property', 'installationPath'],
                                         { encoding: 'utf8' }).trim();

        // Look for the VC toolset directory in the installation.
        // Common paths: VC\Tools\MSVC\<version>\bin\Hostx64\<arch>
        const vcToolsPath = path.join(installPath, 'VC', 'Tools', 'MSVC');
        if (fs.existsSync(vcToolsPath)) {
            const versions = fs.readdirSync(vcToolsPath);
            if (versions.length > 0) {
                // Get the latest version string (e.g., "14.40.33807")
                const latestVersion = versions.sort().reverse()[0];
                // Map compiler version to toolset name.
                // v14.30+ -> v143 (VS 2022)
                // v14.40+ -> v180 (VS 2026)
                // v14.20-29 -> v142 (VS 2019)
                const major = parseInt(latestVersion.split('.')[0]);
                const minor = parseInt(latestVersion.split('.')[1]);

                if (major >= 14 && minor >= 40) {
                    return 'v180'; // VS 2026
                } else if (major >= 14 && minor >= 30) {
                    return 'v143'; // VS 2022
                } else if (major >= 14 && minor >= 20) {
                    return 'v142'; // VS 2019
                }
            }
        }
    } catch (e) {
        // vswhere might not be available or other error
    }

    return null;
}

const toolset = process.argv[2] || findAvailableToolset();

if (!toolset) {
    // No toolset specified and couldn't find one; silently exit.
    process.exit(0);
}

const buildDir = path.join(__dirname, '..', 'native', 'build');
if (!fs.existsSync(buildDir)) {
    process.exit(0);
}

const vcxprojFiles = fs.readdirSync(buildDir)
    .filter((name) => name.endsWith('.vcxproj'))
    .map((name) => path.join(buildDir, name));

let changed = 0;
for (const file of vcxprojFiles) {
    const content = fs.readFileSync(file, 'utf8');
    // Replace any PlatformToolset value with the target toolset
    const next = content.replace(
        /<PlatformToolset>[^<]+<\/PlatformToolset>/g,
        `<PlatformToolset>${toolset}</PlatformToolset>`
    );
    if (next !== content) {
        fs.writeFileSync(file, next, 'utf8');
        changed += 1;
    }
}

if (changed > 0) {
    console.log(`Retargeted ${changed} vcxproj file(s) to ${toolset}.`);
}