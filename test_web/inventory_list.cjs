const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const page = fs.readFileSync('web/index.html', 'utf8');
const begin = page.indexOf('// ---- List continuation:');
const end = page.indexOf('// ---- End list continuation');
assert.ok(begin >= 0 && end > begin, 'the page defines list continuation');
const context = vm.createContext({});
vm.runInContext(page.slice(begin, end) + '\nglobalThis.continueList = continueList;', context);
const { continueList } = context;

function continued(text, caret, selectionEnd = caret) {
  const edit = continueList(text, caret, selectionEnd);
  if (!edit) return null;
  return [text.slice(0, edit.start) + edit.text + text.slice(edit.end), edit.caret];
}

assert.deepEqual(continued('-тест', 5), ['-тест\n-', 7], 'Enter after a dash item starts the next dash item');
assert.deepEqual(continued('Полка\n- шорты', 13), ['Полка\n- шорты\n- ', 16], 'the next item keeps the marker spacing');
assert.deepEqual(continued('• носки', 7), ['• носки\n• ', 10]);
assert.deepEqual(continued('* носки', 7), ['* носки\n* ', 10]);
assert.deepEqual(continued('1. Лыжи', 7), ['1. Лыжи\n2. ', 11], 'numbered items count on');
assert.deepEqual(continued('9) Палки', 8), ['9) Палки\n10) ', 13]);
assert.deepEqual(continued('- шорты', 4), ['- шо\n- рты', 7], 'Enter inside an item splits it into two items');
assert.deepEqual(continued('- шорты\n- ', 10), ['- шорты\n', 8], 'Enter on an empty item ends the list');
assert.deepEqual(continued('-тест\n-', 7), ['-тест\n', 6]);
assert.equal(continued('Шорты', 5), null, 'plain lines keep an ordinary newline');
assert.equal(continued('1.5 м кабель', 12), null, 'a decimal number is not a list marker');
assert.equal(continued('- шорты', 1), null, 'Enter inside the marker keeps an ordinary newline');
assert.equal(continued('', 0), null);
assert.equal(continued('\n- a', 0), null, 'the line before the caret is used');
assert.equal(continued('- шорты', 2, 5), null, 'a selection keeps an ordinary newline');

console.log('inventory list checks passed');
