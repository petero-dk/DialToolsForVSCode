const fs = require('fs');
const path = require('path');

const toolset = process.argv[2];
if (!toolset) {
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
  const next = content.replace(
    /<PlatformToolset>v143<\/PlatformToolset>/g,
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