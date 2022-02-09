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

const visibleArrowsObject = new Arrow(timeline, visibleArrows);

function onSelect(properties) {
    // Clear the visible arrows array
    while (visibleArrows.length > 0) {
        visibleArrowsObject.removeArrow(visibleArrows[visibleArrows.length - 1].id);
    }
    // Arrows incident to selected nodes will be visible
    const selectedNodes = new Set(properties.items);
    for (const arrow of allArrows) {
        // If an arrow is incident to a selected node, make it visible
        if (selectedNodes.has(arrow.id_item_1) || selectedNodes.has(arrow.id_item_2)) {
            visibleArrowsObject.addArrow(arrow);
        }
    }
    for (const arrow of criticalPathArrows) {
        // If a critical path arrow has not yet been made visible, make it visible
        if (!selectedNodes.has(arrow.id_item_1) && !selectedNodes.has(arrow.id_item_2)) {
            visibleArrowsObject.addArrow(arrow);
        }
    }
}

timeline.on('select', onSelect);
