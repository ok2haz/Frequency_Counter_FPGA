// Quick coverage check: which needed codepoints exist in the TTFs.
const path = require('path');
const REPO = path.resolve(__dirname, '..', '..', '..', '..');
const opentype = require(path.join(REPO, 'tools', 'node-v20.18.1-win-x64',
    'node_modules', 'lv_font_conv', 'node_modules', 'opentype.js'));
const TTF = path.join(REPO, 'tools', 'ttf');

const cps = {
    'sup-(207B)': 0x207B, 'sup0(2070)': 0x2070, 'sup1(00B9)': 0x00B9,
    'sup2(00B2)': 0x00B2, 'sup3(00B3)': 0x00B3, 'sup4(2074)': 0x2074,
    'sup9(2079)': 0x2079, 'middot(00B7)': 0x00B7, 'times(00D7)': 0x00D7,
    'play(25B6)': 0x25B6, 'bullet(25CF)': 0x25CF, 'equiv(2261)': 0x2261,
    'Omega(03A9)': 0x03A9, 'sigma(03C3)': 0x03C3, 'tau(03C4)': 0x03C4,
    'pm(00B1)': 0x00B1,
};
for (const ttf of ['JetBrainsMono-Medium.ttf', 'Inter-Regular.ttf']) {
    const f = opentype.loadSync(path.join(TTF, ttf));
    let line = ttf + ': ';
    for (const [name, cp] of Object.entries(cps)) {
        const g = f.charToGlyph(String.fromCodePoint(cp));
        line += name + '=' + (g && g.index !== 0 ? 'Y' : '-') + ' ';
    }
    console.log(line);
}
