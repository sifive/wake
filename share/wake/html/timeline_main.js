/* Copyright 2022 SiFive, Inc.
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
const container = document.getElementById('timeline');

const groups = new vis.DataSet([
    {id: 0, content: "Other Jobs", value: 0},
    {id: 1, content: "Critical Path", value: 1},
]);

// Order by job length
function customOrder(jobA, jobB) {
    return (jobB.end - jobB.start) - (jobA.end - jobA.start);
}

// Configuration for the Timeline
const options = {
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

const jobs = JSON.parse(document.getElementById("jobs").textContent);

// Create jobs Timeline
const timeline = new vis.Timeline(container, jobs, options, groups);

// An Arrow object has an id, id_item_1 (the id of a job), and id_item_2 (the id of that job's dependency).
// All Arrows, however, point into the future, regardless of which job ended later.

// criticalPathArrows describes the critical path ending with a job with no dependencies.
const criticalPathArrows = JSON.parse(document.getElementById("criticalPathArrows").textContent);
const allArrows = JSON.parse(document.getElementById("allArrows").textContent);
const visibleArrows = [...criticalPathArrows];

const arrowObject = new Arrow(timeline, visibleArrows);

function addNodeArrows(node, added) {
    for (const arrow of allArrows) {
        if ((arrow.id_item_1 === node && !(added.includes(arrow.id_item_2))) ||
            (arrow.id_item_2 === node && !(added.includes(arrow.id_item_1)))) {
            arrowObject.addArrow(arrow);
        }
    }
}

function onSelect(properties) {
    while (visibleArrows.length > 0) {
        arrowObject.removeArrow(visibleArrows[visibleArrows.length - 1].id);
    }
    const added = [];
    for (const item of properties.items) {
        addNodeArrows(item, added);
        added.push(item);
    }
    for (const arrow of criticalPathArrows) {
        if (!added.includes(arrow.id_item_1) && !added.includes(arrow.id_item_2)) {
            arrowObject.addArrow(arrow);
        }
    }
}

timeline.on('select', onSelect);
