# Run this adjacent to the Release build of the Vantage.app dir

fs = require 'fs'
{execSync} = require 'child_process'

versionHeader = "#{__dirname}/../../common/version.h"
versionContents = fs.readFileSync(versionHeader, "utf8")
lines = versionContents.split(/\n/)
version = null
for line in lines
  if matches = line.match(/#define VANTAGE_VERSION (.+)/)
    version = matches[1]
    break

if version == null
  console.log "Can't find Vantage version!"
  process.exit(-1)

dmgbuildSettings = "#{__dirname}/dmgbuildSettings.py"

outputFilename = "vantage-#{version}.dmg"
console.log "Generating: #{outputFilename}"

cmd = "dmgbuild -s #{dmgbuildSettings} -D app=Vantage.app \"Vantage #{version}\" #{outputFilename}"
console.log "Executing: #{cmd}"
execSync(cmd, { stdio: 'inherit' })
