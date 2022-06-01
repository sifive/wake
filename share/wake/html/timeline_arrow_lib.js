/**
 * timeline-arrows
 * https://github.com/javdome/timeline-arrows
 *
 * Class to easily draw lines to connect items in the vis Timeline module.
 *
 * @version 3.1.0
 * @date    2021-04-06
 *
 * @copyright (c) Javi Domenech (javdome@gmail.com)
 *
 *
 * @license
 * timeline-arrows is dual licensed under both
 *
 *   1. The Apache 2.0 License
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *   and
 *
 *   2. The MIT License
 *      http://opensource.org/licenses/MIT
 *
 * timeline-arrows may be distributed under either license.
 */

class Arrow {

    constructor(timeline, dependencies) {
        this._svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
        this._timeline = timeline;

        this._arrowHead = document.createElementNS(
            "http://www.w3.org/2000/svg",
            "marker"
        );
        this._arrowHeadPath = document.createElementNS(
            "http://www.w3.org/2000/svg",
            "path"
        );

        this._dependency = dependencies;

        this._dependencyPath = [];

        this._initialize();
    }

    _initialize() {
        //Configures the SVG layer and add it to timeline
        this._svg.style.position = "absolute";
        this._svg.style.top = "0px";
        this._svg.style.height = "100%";
        this._svg.style.width = "100%";
        this._svg.style.display = "block";
        this._svg.style.zIndex = "1"; // Should it be above or below? (1 for above, -1 for below)
        this._svg.style.pointerEvents = "none"; // To click through, if we decide to put it above other elements.
        this._timeline.dom.center.appendChild(this._svg);

        //Configure the arrowHead
        this._arrowHead.setAttribute("id", "arrowhead0");
        this._arrowHead.setAttribute("viewBox", "-10 -5 10 10");
        this._arrowHead.setAttribute("refX", "-7");
        this._arrowHead.setAttribute("refY", "0");
        this._arrowHead.setAttribute("markerUnits", "strokeWidth");
        this._arrowHead.setAttribute("markerWidth", "3");
        this._arrowHead.setAttribute("markerHeight", "3");
        this._arrowHead.setAttribute("orient", "auto");
        //Configure the path of the arrowHead (arrowHeadPath)
        this._arrowHeadPath.setAttribute("d", "M 0 0 L -10 -5 L -7.5 0 L -10 5 z");
        this._arrowHeadPath.style.fill = "#9c0000";
        this._arrowHead.appendChild(this._arrowHeadPath);
        this._svg.appendChild(this._arrowHead);
        //Create paths for the started dependency array
        for (let i = 0; i < this._dependency.length; i++) {
            this._createPath();
        }

        //NOTE: We hijack the on "changed" event to draw the arrows.
        this._timeline.on("changed", () => {
            this._drawDependencies();
        });

    }

    _createPath(){
        //Add a new path to array dependencyPath and to svg
        let somePath = document.createElementNS(
            "http://www.w3.org/2000/svg",
            "path"
        );
        somePath.setAttribute("d", "M 0 0");
        somePath.style.stroke = "#9c0000";
        somePath.style.strokeWidth = "3px";
        somePath.style.fill = "none";
        somePath.style.pointerEvents = "auto";
        this._dependencyPath.push(somePath);
        this._svg.appendChild(somePath);
    }



    _drawDependencies() {
        //Create paths for the started dependency array
        for (let i = 0; i < this._dependency.length; i++) {
            this._drawArrows(this._dependency[i], i);
        }
    }

    _drawArrows(dep, index) {
        //Checks if both items exist
        //if( (typeof this._timeline.itemsData._data[dep.id_item_1] !== "undefined") && (typeof this._timeline.itemsData._data[dep.id_item_2] !== "undefined") ) {
        //debugger;
        if( (this._timeline.itemsData.get(dep.id_item_1) !== null) && (this._timeline.itemsData.get(dep.id_item_2) !== null) ) {
            var bothItemsExist = true;
        } else {
            var bothItemsExist = false;
        }

        //Checks if at least one item is visible in screen
        var oneItemVisible = false; //Iniciamos a false
        if (bothItemsExist) {
            var visibleItems = this._timeline.getVisibleItems();
            for (let k = 0; k < visibleItems.length ; k++) {
                if (dep.id_item_1 == visibleItems[k]) oneItemVisible = true;
                if (dep.id_item_2 == visibleItems[k]) oneItemVisible = true;
            }

            //Checks if the groups of items are both visible
            var groupOf_1_isVisible = false; //Iniciamos a false
            var groupOf_2_isVisible = false; //Iniciamos a false

            let groupOf_1 = this._timeline.itemsData.get(dep.id_item_1).group; //let groupOf_1 = items.get(dep.id_item_1).group;

            let groupOf_2 = this._timeline.itemsData.get(dep.id_item_2).group; //let groupOf_2 = items.get(dep.id_item_2).group;

            if ( this._timeline.groupsData.get(groupOf_1) ) groupOf_1_isVisible = true;

            if ( this._timeline.groupsData.get(groupOf_2) ) groupOf_2_isVisible = true;


            // If groups are null then they are not visible.
            if (groupOf_1 == null){
                var groupOf_1_isVisible = false;
            }
            if (groupOf_2 == null){
                var groupOf_2_isVisible = false;
            }
        }

        if ( (groupOf_1_isVisible && groupOf_2_isVisible) && (oneItemVisible) && (bothItemsExist)) {
            var item_1 = this._getItemPos(this._timeline.itemSet.items[dep.id_item_1]);
            var item_2 = this._getItemPos(this._timeline.itemSet.items[dep.id_item_2]);
            if (item_2.mid_x < item_1.mid_x) [item_1, item_2] = [item_2, item_1]; // As demo, we put an arrow between item 0 and item1, from the one that is more on left to the one more on right.
            var curveLen = item_1.height * 2; // Length of straight Bezier segment out of the item.
            item_2.left -= 10; // Space for the arrowhead.
            this._dependencyPath[index].setAttribute("marker-end", "url(#arrowhead0)");
            this._dependencyPath[index].setAttribute(
                "d",
                "M " +
                item_1.right +
                " " +
                item_1.mid_y +
                " C " +
                (item_1.right + curveLen) +
                " " +
                item_1.mid_y +
                " " +
                (item_2.left - curveLen) +
                " " +
                item_2.mid_y +
                " " +
                item_2.left +
                " " +
                item_2.mid_y
            );
            // Adding the title if property title has been added in the dependency
            if (dep.hasOwnProperty("title")) {
                this._dependencyPath[index].innerHTML = "<title>" +dep.title +"</title>"
            }
        } else {
            this._dependencyPath[index].setAttribute("marker-end", "");
            this._dependencyPath[index].setAttribute("d", "M 0 0");
        }

    }

    //Función que recibe in Item y devuelve la posición en pantalla del item.
    _getItemPos (item) {
        let left_x = item.left;
        let top_y = item.parent.top + item.parent.height - item.top - item.height;
        return {
            left: left_x,
            top: top_y,
            right: left_x + item.width,
            bottom: top_y + item.height,
            mid_x: left_x + item.width / 2,
            mid_y: top_y + item.height / 2,
            width: item.width,
            height: item.height
        }
    }


    addArrow (dep) {
        this._dependency.push(dep);
        this._createPath();
        this._timeline.redraw();
    }

    getArrow (id) {
        for (let i = 0; i < this._dependency.length; i++) {
            if (this._dependency[i].id == id) {
                return this._dependency[i];
            }
        }
        return null;
    }

    //Función que recibe el id de una flecha y la elimina.
    removeArrow(id) {
        for (let i = 0; i < this._dependency.length; i++) {
            if (this._dependency[i].id == id) var index = i;
        }

        //var list = document.getElementsByTagName("path"); //FALTA QUE ESTA SELECCION LA HAGA PARA EL DOM DEL TIMELINE INSTANCIADO!!!!
        var list = document.querySelectorAll("#" +this._timeline.dom.container.id +" path");

        this._dependency.splice(index, 1); //Elimino del array dependency
        this._dependencyPath.splice(index, 1); //Elimino del array dependencyPath

        list[index + 1].parentNode.removeChild(list[index + 1]); //Lo elimino del dom
    }

    //Función que recibe el id de un item y elimina la flecha.
    removeArrowbyItemId(id) {
        let listOfRemovedArrows = [];
        for (let i = 0; i < this._dependency.length; i++) {
            if ( (this._dependency[i].id_item_1 == id) || (this._dependency[i].id_item_2 == id) ) {
                listOfRemovedArrows.push(this._dependency[i].id);
                this.removeArrow(this._dependency[i].id);
                i--;
            }
        }
        return listOfRemovedArrows;
    }
}
