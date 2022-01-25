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

// DOM element where the Timeline will be attached
let container = document.getElementById('timeline');

const groups = new vis.DataSet([
    {id: 0, content: "Other Jobs", value: 0},
    {id: 1, content: "Critical Path", value: 1},
]);

// Order by job length
function customOrder(jobs, accesses) {
    return (accesses.end - accesses.start) - (jobs.end - jobs.start);
}

// Configuration for the Timeline
let options = {
    order: customOrder,
    stack: true,
    verticalScroll: true,
    zoomKey: "ctrlKey",
    width: '100%',
    maxHeight: '700px',
    tooltip: {
        followMouse: true,
        overflowMethod: "cap",
    },
    multiselect: true,
};

// Create jobs Timeline
let timeline = new vis.Timeline(container, jobs, options, groups);

let visible_arrows = [...critical_path_arrows];
const arrowObject = new Arrow(timeline, visible_arrows);

function addNodeArrows(node, added) {
    for (const arrow of all_arrows) {
        if ((arrow.id_item_1 === node && !(added.includes(arrow.id_item_2))) ||
            (arrow.id_item_2 === node && !(added.includes(arrow.id_item_1)))) {
            arrowObject.addArrow(arrow);
        }
    }
}

function onSelect(properties) {
    while (visible_arrows.length > 0) {
        arrowObject.removeArrow(visible_arrows[visible_arrows.length - 1].id);
    }
    let added = [];
    for (const item of properties.items) {
        addNodeArrows(item, added);
        added.push(item);
    }
    for (const arrow of critical_path_arrows) {
        if (!(added.includes(arrow.id_item_1)) && !(added.includes(arrow.id_item_2))) {
            arrowObject.addArrow(arrow);
        }
    }
}

timeline.on('select', onSelect);
