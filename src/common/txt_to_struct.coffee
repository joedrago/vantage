fs = require 'fs'

lines = fs.readFileSync("mono.txt", "utf8").split(/\n/)
glyphs = []
common = {}
for line in lines
  if matches = line.match(/^common (.+)$/)
    pieces = matches[1].split(/ /)
    for piece in pieces
      kv = piece.split(/=/)
      common[kv[0]] = parseInt(kv[1])
  if matches = line.match(/^char (.+)$/)
    pieces = matches[1].split(/ /)
    glyph = {}
    for piece in pieces
      kv = piece.split(/=/)
      glyph[kv[0]] = parseInt(kv[1])
    glyphs.push glyph


console.log "static const int GLYPH_LINEHEIGHT = #{common.lineHeight};";
console.log "static const int GLYPH_BASE = #{common.base};";
console.log "static const int GLYPH_SCALEW = #{common.scaleW};";
console.log "static const int GLYPH_SCALEH = #{common.scaleH};";
console.log "typedef struct Glyph { int id; int x; int y; int width; int height; int xoffset; int yoffset; int advance; } Glyph;"
console.log "\nGlyph monoGlyphs[] = {"
for g, index in glyphs
  # console.log "    { #{g.id}, #{(g.x / common.scaleW).toFixed(5)}f, #{(g.y / common.scaleW).toFixed(5)}f, #{(g.width / common.scaleW).toFixed(5)}f, #{(g.height / common.scaleW).toFixed(5)}f, #{(g.xoffset / common.scaleW).toFixed(5)}f, #{(g.yoffset / common.scaleW).toFixed(5)}f, #{(g.xadvance / common.scaleW).toFixed(5)}f }#{ if (index != glyphs.length - 1) then "," else ""} "
  console.log "    { #{g.id}, #{g.x}, #{g.y}, #{g.width}, #{g.height}, #{g.xoffset}, #{g.yoffset}, #{g.xadvance} }#{ if (index != glyphs.length - 1) then "," else ""} "
console.log "};"
