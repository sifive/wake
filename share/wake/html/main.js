/* Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

'use strict';

// tooltip
const tooltip = document.createElement('div');
tooltip.classList.add('tooltip');
tooltip.style.visibility = 'hidden';

// css to select uses
const usecss = document.createElement('div');
usecss.appendChild(document.createElement('style'));

// The following variables encompass the state for expression selection mode,
// where pressing + or - on the keyboard while the mouse is hovering over an
// expression will allow one to select parent or child expressions, which
// affects the type shown in the tooltip.

/** The current expression selection depth. */
let depth = 0;
/** The inner-most (depth 0) element in the expression tree. */
let inner = null;
/** The currently-selected element in the expression tree. */
let select = null;

/** Update the currently selected expression.
 *
 * The global variables `depth` and `inner` are essentially the arguments to the
 * function, used to determine which expression should be selected. `inner` is
 * the smallest expression that the mouse is hovering over. `depth` is how many
 * parent expressions to walk up.
 */
function update () {
  let next = inner;
  for (let i = 0; i < depth; ++i) {
    let parent = next?.parentElement;
    if (!parent?.getAttribute('sourceType')) {
      depth = i;
      break;
    }
    next = parent;
  }
  if (next != select) {
    if (select) {
      select.removeAttribute('focus');
      tooltip.style.visibility = 'hidden';
    }
    if (next) {
      next.setAttribute('focus', 'true');
      tooltip.style.visibility = 'visible';
      tooltip.innerHTML = next.getAttribute('sourceType');
    }
  }
  select = next;
}

function onMouseOver(event) {
  inner = event.target;
  depth = 0;
  update();
  tooltip.style.cssText = `position: absolute; top: ${event.pageY + 10}px; left: ${event.pageX}px;`;
  event.stopPropagation();
};

function onMouseOut(event) {
  inner = null;
  depth = 0;
  update();
  event.stopPropagation();
};

document.addEventListener('keypress', function(event) {
  const char = event.which || event.keyCode;
  if (char === 43 || char == 61) { ++depth; update(); }
  if ((char === 45 || char == 95) && depth > 0) { --depth; update(); }
}, true);

function smoothFromTo(fromX, fromY, destX, destY, step) {
  const steps = 50;
  const stepMs = 20;
  const ratio = 0.01; // 0=jump < ratio < 1=linear

  const progress = (Math.exp(-Math.log(ratio) * (steps - step) / steps) - 1) * ratio / (1 - ratio);
  const stepX = destX + progress * (fromX - destX);
  const stepY = destY + progress * (fromY - destY);

  window.scrollTo(stepX, stepY);

  if (step < steps)
    setTimeout(function () { smoothFromTo(fromX, fromY, destX, destY, step+1); }, stepMs);
}

function focusOn(event) {
  const target = event.currentTarget.getAttribute('href').substring(1);
  const where = document.getElementById(target).getBoundingClientRect();
  const fromX = window.scrollX;
  const fromY = window.scrollY;
  const destX = (where.left + where.right  - window.innerWidth)  / 2 + fromX;
  const destY = (where.top  + where.bottom - window.innerHeight) / 2 + fromY;

  usecss.firstChild.innerHTML = '*[id=\'' + target + '\'] { background-color: red; }';
  window.location.hash = target;
  event.preventDefault();

  smoothFromTo(fromX, fromY, destX, destY, 1);
};

function onMouseClick(event) {
  usecss.firstChild.innerHTML = '*[href=\'#' + event.currentTarget.id + '\'] { background-color: red; }';
  event.stopPropagation();
};

function render(root) {
  const str = utf8.encode(root.source);

  function rec (node) {
    const pRange = node.range;
    const body = node.body;

    let res = document.createElement(node.target ? 'a' : 'span');
    res.setAttribute('class', node.type);

    if (node.sourceType) {
      res.setAttribute('sourceType', node.sourceType);
      res.addEventListener('mouseover', onMouseOver);
      res.addEventListener('mouseout', onMouseOut);
    }

    if (node.type === 'VarDef' || node.type === 'VarArg') {
      res.setAttribute('id', root.filename + ':' + node.range.join(':'));
      res.addEventListener('click', onMouseClick);
    }

    if (node.target) {
      res.setAttribute('href', '#' + node.target.filename + ':' + node.target.range.join(':'));
      res.addEventListener('click', focusOn);
    }

    let pointer = pRange[0];
    if (body) body.map(child => {
      const cRange = child.range;
      if (pointer < cRange[0]) {
        const text = utf8.decode(str.slice(pointer, cRange[0]));
        res.appendChild(document.createTextNode(text));
      }
      res.appendChild(rec(child));
      pointer = cRange[1];
    });

    if (pointer < pRange[1]) {
      const text = utf8.decode(str.slice(pointer, pRange[1]));
      res.appendChild(document.createTextNode(text));
    }

    return res;
  };

  return rec(root);
};

function program(node) {
  let h2 = document.createElement('h2');
  h2.appendChild(document.createTextNode(node.filename || '---'));
  let pre = document.createElement('pre');
  pre.appendChild(render(node));
  let res = document.createElement('div');
  res.appendChild(document.createElement('hr'));
  res.appendChild(h2);
  res.appendChild(pre);
  return res;
};

function workspace(node) {
  let res = document.createElement('div');
  for (let p of node.body || [])
    res.appendChild(program(p));
  return res;
}

document.addEventListener('DOMContentLoaded', function main () {
  document.body.appendChild(usecss);
  document.body.appendChild(tooltip);
  const wakeData = document.getElementById('wake-data');
  const node = JSON.parse(wakeData.text);
  document.body.appendChild(workspace(node));
});

/* eslint-env browser */
