(function() {

  var IOS = navigator.userAgent.match(/iP(?:hone|ad;(?: U;)? CPU) OS (\d+)/);
  var IOS_TOUCH_SCROLLING = IOS && IOS[1] >= 8;
  var DEFAULT_PHYSICAL_COUNT = 3;
  var MAX_PHYSICAL_COUNT = 500;
  var HIDDEN_Y = '-10000px';

  Polymer({

    is: 'iron-list',

    properties: {

      /**
       * An array containing items determining how many instances of the template
       * to stamp and that that each template instance should bind to.
       */
      items: {
        type: Array
      },

      /**
       * The name of the variable to add to the binding scope for the array
       * element associated with a given template instance.
       */
      as: {
        type: String,
        value: 'item'
      },

      /**
       * The name of the variable to add to the binding scope with the index
       * for the row.
       */
      indexAs: {
        type: String,
        value: 'index'
      },

      /**
       * The name of the variable to add to the binding scope to indicate
       * if the row is selected.
       */
      selectedAs: {
        type: String,
        value: 'selected'
      },

      /**
       * When true, tapping a row will select the item, placing its data model
       * in the set of selected items retrievable via the selection property.
       *
       * Note that tapping focusable elements within the list item will not
       * result in selection, since they are presumed to have their * own action.
       */
      selectionEnabled: {
        type: Boolean,
        value: false
      },

      /**
       * When `multiSelection` is false, this is the currently selected item, or `null`
       * if no item is selected.
       */
      selectedItem: {
        type: Object,
        notify: true
      },

      /**
       * When `multiSelection` is true, this is an array that contains the selected items.
       */
      selectedItems: {
        type: Object,
        notify: true
      },

      /**
       * When `true`, multiple items may be selected at once (in this case,
       * `selected` is an array of currently selected items).  When `false`,
       * only one item may be selected at a time.
       */
      multiSelection: {
        type: Boolean,
        value: false
      }
    },

    observers: [
      '_itemsChanged(items.*)',
      '_selectionEnabledChanged(selectionEnabled)',
      '_multiSelectionChanged(multiSelection)',
      '_setOverflow(scrollTarget)'
    ],

    behaviors: [
      Polymer.Templatizer,
      Polymer.IronResizableBehavior,
      Polymer.IronA11yKeysBehavior,
      Polymer.IronScrollTargetBehavior
    ],

    listeners: {
      'iron-resize': '_resizeHandler'
    },

    keyBindings: {
      'up': '_didMoveUp',
      'down': '_didMoveDown',
      'enter': '_didEnter'
    },

    /**
     * The ratio of hidden tiles that should remain in the scroll direction.
     * Recommended value ~0.5, so it will distribute tiles evely in both directions.
     */
    _ratio: 0.5,

    /**
     * The padding-top value for the list.
     */
    _scrollerPaddingTop: 0,

    /**
     * This value is the same as `scrollTop`.
     */
    _scrollPosition: 0,

    /**
     * The sum of the heights of all the tiles in the DOM.
     */
    _physicalSize: 0,

    /**
     * The average `F` of the tiles observed till now.
     */
    _physicalAverage: 0,

    /**
     * The number of tiles which `offsetHeight` > 0 observed until now.
     */
    _physicalAverageCount: 0,

    /**
     * The Y position of the item rendered in the `_physicalStart`
     * tile relative to the scrolling list.
     */
    _physicalTop: 0,

    /**
     * The number of items in the list.
     */
    _virtualCount: 0,

    /**
     * A map between an item key and its physical item index
     */
    _physicalIndexForKey: null,

    /**
     * The estimated scroll height based on `_physicalAverage`
     */
    _estScrollHeight: 0,

    /**
     * The scroll height of the dom node
     */
    _scrollHeight: 0,

    /**
     * The height of the list. This is referred as the viewport in the context of list.
     */
    _viewportSize: 0,

    /**
     * An array of DOM nodes that are currently in the tree
     * @type {?Array<!TemplatizerNode>}
     */
    _physicalItems: null,

    /**
     * An array of heights for each item in `_physicalItems`
     * @type {?Array<number>}
     */
    _physicalSizes: null,

    /**
     * A cached value for the first visible index.
     * See `firstVisibleIndex`
     * @type {?number}
     */
    _firstVisibleIndexVal: null,

    /**
     * A cached value for the last visible index.
     * See `lastVisibleIndex`
     * @type {?number}
     */
    _lastVisibleIndexVal: null,

    /**
     * A Polymer collection for the items.
     * @type {?Polymer.Collection}
     */
    _collection: null,

    /**
     * True if the current item list was rendered for the first time
     * after attached.
     */
    _itemsRendered: false,

    /**
     * The page that is currently rendered.
     */
    _lastPage: null,

    /**
     * The max number of pages to render. One page is equivalent to the height of the list.
     */
    _maxPages: 3,

    /**
     * The currently focused physical item.
     */
    _focusedItem: null,

    /**
     * The index of the `_focusedItem`.
     */
    _focusedIndex: -1,

    /**
     * The the item that is focused if it is moved offscreen.
     * @private {?TemplatizerNode}
     */
    _offscreenFocusedItem: null,

    /**
     * The item that backfills the `_offscreenFocusedItem` in the physical items
     * list when that item is moved offscreen.
     */
    _focusBackfillItem: null,

    /**
     * The bottom of the physical content.
     */
    get _physicalBottom() {
      return this._physicalTop + this._physicalSize;
    },

    /**
     * The bottom of the scroll.
     */
    get _scrollBottom() {
      return this._scrollPosition + this._viewportSize;
    },

    /**
     * The n-th item rendered in the last physical item.
     */
    get _virtualEnd() {
      return this._virtualStart + this._physicalCount - 1;
    },

    /**
     * The height of the physical content that isn't on the screen.
     */
    get _hiddenContentSize() {
      return this._physicalSize - this._viewportSize;
    },

    /**
     * The maximum scroll top value.
     */
    get _maxScrollTop() {
      return this._estScrollHeight - this._viewportSize + this._scrollerPaddingTop;
    },

    /**
     * The lowest n-th value for an item such that it can be rendered in `_physicalStart`.
     */
    _minVirtualStart: 0,

    /**
     * The largest n-th value for an item such that it can be rendered in `_physicalStart`.
     */
    get _maxVirtualStart() {
      return Math.max(0, this._virtualCount - this._physicalCount);
    },

    /**
     * The n-th item rendered in the `_physicalStart` tile.
     */
    _virtualStartVal: 0,

    set _virtualStart(val) {
      this._virtualStartVal = Math.min(this._maxVirtualStart, Math.max(this._minVirtualStart, val));
    },

    get _virtualStart() {
      return this._virtualStartVal || 0;
    },

    /**
     * The k-th tile that is at the top of the scrolling list.
     */
    _physicalStartVal: 0,

    set _physicalStart(val) {
      this._physicalStartVal = val % this._physicalCount;
      if (this._physicalStartVal < 0) {
        this._physicalStartVal = this._physicalCount + this._physicalStartVal;
      }
      this._physicalEnd = (this._physicalStart + this._physicalCount - 1) % this._physicalCount;
    },

    get _physicalStart() {
      return this._physicalStartVal || 0;
    },

    /**
     * The number of tiles in the DOM.
     */
    _physicalCountVal: 0,

    set _physicalCount(val) {
      this._physicalCountVal = val;
      this._physicalEnd = (this._physicalStart + this._physicalCount - 1) % this._physicalCount;
    },

    get _physicalCount() {
      return this._physicalCountVal;
    },

    /**
     * The k-th tile that is at the bottom of the scrolling list.
     */
    _physicalEnd: 0,

    /**
     * An optimal physical size such that we will have enough physical items
     * to fill up the viewport and recycle when the user scrolls.
     *
     * This default value assumes that we will at least have the equivalent
     * to a viewport of physical items above and below the user's viewport.
     */
    get _optPhysicalSize() {
      return this._viewportSize * this._maxPages;
    },

   /**
    * True if the current list is visible.
    */
    get _isVisible() {
      return this.scrollTarget && Boolean(this.scrollTarget.offsetWidth || this.scrollTarget.offsetHeight);
    },

    /**
     * Gets the index of the first visible item in the viewport.
     *
     * @type {number}
     */
    get firstVisibleIndex() {
      if (this._firstVisibleIndexVal === null) {
        var physicalOffset = this._physicalTop + this._scrollerPaddingTop;

        this._firstVisibleIndexVal = this._iterateItems(
          function(pidx, vidx) {
            physicalOffset += this._physicalSizes[pidx];
            if (physicalOffset > this._scrollPosition) {
              return vidx;
            }
          }) || 0;
      }
      return this._firstVisibleIndexVal;
    },

    /**
     * Gets the index of the last visible item in the viewport.
     *
     * @type {number}
     */
    get lastVisibleIndex() {
      if (this._lastVisibleIndexVal === null) {
        var physicalOffset = this._physicalTop;

        this._iterateItems(function(pidx, vidx) {
          physicalOffset += this._physicalSizes[pidx];

          if (physicalOffset <= this._scrollBottom) {
            this._lastVisibleIndexVal = vidx;
          }
        });
      }
      return this._lastVisibleIndexVal;
    },

    get _defaultScrollTarget() {
      return this;
    },

    ready: function() {
      this.addEventListener('focus', this._didFocus.bind(this), true);
    },

    attached: function() {
      this.updateViewportBoundaries();
      this._render();
    },

    detached: function() {
      this._itemsRendered = false;
    },

    /**
     * Set the overflow property if this element has its own scrolling region
     */
    _setOverflow: function(scrollTarget) {
      this.style.webkitOverflowScrolling = scrollTarget === this ? 'touch' : '';
      this.style.overflow = scrollTarget === this ? 'auto' : '';
    },

    /**
     * Invoke this method if you dynamically update the viewport's
     * size or CSS padding.
     *
     * @method updateViewportBoundaries
     */
    updateViewportBoundaries: function() {
      this._scrollerPaddingTop = this.scrollTarget === this ? 0 :
          parseInt(window.getComputedStyle(this)['padding-top'], 10);

      this._viewportSize = this._scrollTargetHeight;
    },

    /**
     * Update the models, the position of the
     * items in the viewport and recycle tiles as needed.
     */
    _scrollHandler: function() {
      // clamp the `scrollTop` value
      var scrollTop = Math.max(0, Math.min(this._maxScrollTop, this._scrollTop));
      var delta = scrollTop - this._scrollPosition;
      var tileHeight, tileTop, kth, recycledTileSet, scrollBottom, physicalBottom;
      var ratio = this._ratio;
      var recycledTiles = 0;
      var hiddenContentSize = this._hiddenContentSize;
      var currentRatio = ratio;
      var movingUp = [];

      // track the last `scrollTop`
      this._scrollPosition = scrollTop;

      // clear cached visible indexes
      this._firstVisibleIndexVal = null;
      this._lastVisibleIndexVal = null;

      scrollBottom = this._scrollBottom;
      physicalBottom = this._physicalBottom;

      // random access
      if (Math.abs(delta) > this._physicalSize) {
        this._physicalTop += delta;
        recycledTiles =  Math.round(delta / this._physicalAverage);
      }
      // scroll up
      else if (delta < 0) {
        var topSpace = scrollTop - this._physicalTop;
        var virtualStart = this._virtualStart;

        recycledTileSet = [];

        kth = this._physicalEnd;
        currentRatio = topSpace / hiddenContentSize;

        // move tiles from bottom to top
        while (
            // approximate `currentRatio` to `ratio`
            currentRatio < ratio &&
            // recycle less physical items than the total
            recycledTiles < this._physicalCount &&
            // ensure that these recycled tiles are needed
            virtualStart - recycledTiles > 0 &&
            // ensure that the tile is not visible
            physicalBottom - this._physicalSizes[kth] > scrollBottom
        ) {

          tileHeight = this._physicalSizes[kth];
          currentRatio += tileHeight / hiddenContentSize;
          physicalBottom -= tileHeight;
          recycledTileSet.push(kth);
          recycledTiles++;
          kth = (kth === 0) ? this._physicalCount - 1 : kth - 1;
        }

        movingUp = recycledTileSet;
        recycledTiles = -recycledTiles;
      }
      // scroll down
      else if (delta > 0) {
        var bottomSpace = physicalBottom - scrollBottom;
        var virtualEnd = this._virtualEnd;
        var lastVirtualItemIndex = this._virtualCount-1;

        recycledTileSet = [];

        kth = this._physicalStart;
        currentRatio = bottomSpace / hiddenContentSize;

        // move tiles from top to bottom
        while (
            // approximate `currentRatio` to `ratio`
            currentRatio < ratio &&
            // recycle less physical items than the total
            recycledTiles < this._physicalCount &&
            // ensure that these recycled tiles are needed
            virtualEnd + recycledTiles < lastVirtualItemIndex &&
            // ensure that the tile is not visible
            this._physicalTop + this._physicalSizes[kth] < scrollTop
          ) {

          tileHeight = this._physicalSizes[kth];
          currentRatio += tileHeight / hiddenContentSize;

          this._physicalTop += tileHeight;
          recycledTileSet.push(kth);
          recycledTiles++;
          kth = (kth + 1) % this._physicalCount;
        }
      }

      if (recycledTiles === 0) {
        // If the list ever reach this case, the physical average is not significant enough
        // to create all the items needed to cover the entire viewport.
        // e.g. A few items have a height that differs from the average by serveral order of magnitude.
        if (physicalBottom < scrollBottom || this._physicalTop > scrollTop) {
          this.async(this._increasePool.bind(this, 1));
        }
      } else {
        this._virtualStart = this._virtualStart + recycledTiles;
        this._physicalStart = this._physicalStart + recycledTiles;
        this._update(recycledTileSet, movingUp);
      }
    },

    /**
     * Update the list of items, starting from the `_virtualStart` item.
     * @param {!Array<number>=} itemSet
     * @param {!Array<number>=} movingUp
     */
    _update: function(itemSet, movingUp) {
      // manage focus
      this._manageFocus();
      // update models
      this._assignModels(itemSet);
      // measure heights
      this._updateMetrics(itemSet);
      // adjust offset after measuring
      if (movingUp) {
        while (movingUp.length) {
          this._physicalTop -= this._physicalSizes[movingUp.pop()];
        }
      }
      // update the position of the items
      this._positionItems();
      // set the scroller size
      this._updateScrollerSize();
      // increase the pool of physical items
      this._increasePoolIfNeeded();
    },

    /**
     * Creates a pool of DOM elements and attaches them to the local dom.
     */
    _createPool: function(size) {
      var physicalItems = new Array(size);

      this._ensureTemplatized();

      for (var i = 0; i < size; i++) {
        var inst = this.stamp(null);
        // First element child is item; Safari doesn't support children[0]
        // on a doc fragment
        physicalItems[i] = inst.root.querySelector('*');
        Polymer.dom(this).appendChild(inst.root);
      }
      return physicalItems;
    },

    /**
     * Increases the pool of physical items only if needed.
     * This function will allocate additional physical items
     * if the physical size is shorter than `_optPhysicalSize`
     */
    _increasePoolIfNeeded: function() {
      if (this._viewportSize === 0 || this._physicalSize >= this._optPhysicalSize) {
        return false;
      }
      // 0 <= `currentPage` <= `_maxPages`
      var currentPage = Math.floor(this._physicalSize / this._viewportSize);
      if (currentPage === 0) {
        // fill the first page
        this._debounceTemplate(this._increasePool.bind(this, Math.round(this._physicalCount * 0.5)));
      } else if (this._lastPage !== currentPage) {
        // paint the page and defer the next increase
        // wait 16ms which is rough enough to get paint cycle.
        Polymer.dom.addDebouncer(this.debounce('_debounceTemplate', this._increasePool.bind(this, 1), 16));
      } else {
        // fill the rest of the pages
        this._debounceTemplate(this._increasePool.bind(this, 1));
      }
      this._lastPage = currentPage;
      return true;
    },

    /**
     * Increases the pool size.
     */
    _increasePool: function(missingItems) {
      var nextPhysicalCount = Math.min(
          this._physicalCount + missingItems,
          this._virtualCount - this._virtualStart,
          MAX_PHYSICAL_COUNT
        );
      var prevPhysicalCount = this._physicalCount;
      var delta = nextPhysicalCount - prevPhysicalCount;

      if (delta <= 0) {
        return;
      }

      [].push.apply(this._physicalItems, this._createPool(delta));
      [].push.apply(this._physicalSizes, new Array(delta));

      this._physicalCount = prevPhysicalCount + delta;

      // update the physical start if we need to preserve the model of the focused item.
      // In this situation, the focused item is currently rendered and its model would
      // have changed after increasing the pool if the physical start remained unchanged.
      if (this._physicalStart > this._physicalEnd &&
          this._isIndexRendered(this._focusedIndex) &&
          this._getPhysicalIndex(this._focusedIndex) < this._physicalEnd) {
        this._physicalStart = this._physicalStart + delta;
      }
      this._update();
    },

    /**
     * Render a new list of items. This method does exactly the same as `update`,
     * but it also ensures that only one `update` cycle is created.
     */
    _render: function() {
      var requiresUpdate = this._virtualCount > 0 || this._physicalCount > 0;

      if (this.isAttached && !this._itemsRendered && this._isVisible && requiresUpdate) {
        this._lastPage = 0;
        this._update();
        this._scrollHandler();
        this._itemsRendered = true;
      }
    },

    /**
     * Templetizes the user template.
     */
    _ensureTemplatized: function() {
      if (!this.ctor) {
        // Template instance props that should be excluded from forwarding
        var props = {};
        props.__key__ = true;
        props[this.as] = true;
        props[this.indexAs] = true;
        props[this.selectedAs] = true;
        props.tabIndex = true;

        this._instanceProps = props;
        this._userTemplate = Polymer.dom(this).querySelector('template');

        if (this._userTemplate) {
          this.templatize(this._userTemplate);
        } else {
          console.warn('iron-list requires a template to be provided in light-dom');
        }
      }
    },

    /**
     * Implements extension point from Templatizer mixin.
     */
    _getStampedChildren: function() {
      return this._physicalItems;
    },

    /**
     * Implements extension point from Templatizer
     * Called as a side effect of a template instance path change, responsible
     * for notifying items.<key-for-instance>.<path> change up to host.
     */
    _forwardInstancePath: function(inst, path, value) {
      if (path.indexOf(this.as + '.') === 0) {
        this.notifyPath('items.' + inst.__key__ + '.' +
          path.slice(this.as.length + 1), value);
      }
    },

    /**
     * Implements extension point from Templatizer mixin
     * Called as side-effect of a host property change, responsible for
     * notifying parent path change on each row.
     */
    _forwardParentProp: function(prop, value) {
      if (this._physicalItems) {
        this._physicalItems.forEach(function(item) {
          item._templateInstance[prop] = value;
        }, this);
      }
    },

    /**
     * Implements extension point from Templatizer
     * Called as side-effect of a host path change, responsible for
     * notifying parent.<path> path change on each row.
     */
    _forwardParentPath: function(path, value) {
      if (this._physicalItems) {
        this._physicalItems.forEach(function(item) {
          item._templateInstance.notifyPath(path, value, true);
        }, this);
      }
    },

    /**
     * Called as a side effect of a host items.<key>.<path> path change,
     * responsible for notifying item.<path> changes.
     */
    _forwardItemPath: function(path, value) {
      if (!this._physicalIndexForKey) {
        return;
      }
      var inst;
      var dot = path.indexOf('.');
      var key = path.substring(0, dot < 0 ? path.length : dot);
      var idx = this._physicalIndexForKey[key];
      var el = this._physicalItems[idx];


      if (idx === this._focusedIndex && this._offscreenFocusedItem) {
        el = this._offscreenFocusedItem;
      }
      if (!el) {
        return;
      }
        
      inst = el._templateInstance;

      if (inst.__key__ !== key) {
        return;
      }
      if (dot >= 0) {
        path = this.as + '.' + path.substring(dot+1);
        inst.notifyPath(path, value, true);
      } else {
        inst[this.as] = value;
      }
    },

    /**
     * Called when the items have changed. That is, ressignments
     * to `items`, splices or updates to a single item.
     */
    _itemsChanged: function(change) {
      if (change.path === 'items') {
        // reset items
        this._virtualStart = 0;
        this._physicalTop = 0;
        this._virtualCount = this.items ? this.items.length : 0;
        this._collection = this.items ? Polymer.Collection.get(this.items) : null;
        this._physicalIndexForKey = {};

        this._resetScrollPosition(0);
        this._removeFocusedItem();

        // create the initial physical items
        if (!this._physicalItems) {
          this._physicalCount = Math.max(1, Math.min(DEFAULT_PHYSICAL_COUNT, this._virtualCount));
          this._physicalItems = this._createPool(this._physicalCount);
          this._physicalSizes = new Array(this._physicalCount);
        }

        this._physicalStart = 0;

      } else if (change.path === 'items.splices') {
        this._adjustVirtualIndex(change.value.indexSplices);
        this._virtualCount = this.items ? this.items.length : 0;

      } else {
        // update a single item
        this._forwardItemPath(change.path.split('.').slice(1).join('.'), change.value);
        return;
      }

      this._itemsRendered = false;
      this._debounceTemplate(this._render);
    },

    /**
     * @param {!Array<!PolymerSplice>} splices
     */
    _adjustVirtualIndex: function(splices) {
      splices.forEach(function(splice) {
        // deselect removed items
        splice.removed.forEach(this._removeItem, this);
        // We only need to care about changes happening above the current position
        if (splice.index < this._virtualStart) {
          var delta = Math.max(
              splice.addedCount - splice.removed.length,
              splice.index - this._virtualStart);

          this._virtualStart = this._virtualStart + delta;

          if (this._focusedIndex >= 0) {
            this._focusedIndex = this._focusedIndex + delta;
          }
        }
      }, this);
    },

    _removeItem: function(item) {
      this.$.selector.deselect(item);
      // remove the current focused item
      if (this._focusedItem && this._focusedItem._templateInstance[this.as] === item) {
        this._removeFocusedItem();
      }
    },

    /**
     * Executes a provided function per every physical index in `itemSet`
     * `itemSet` default value is equivalent to the entire set of physical indexes.
     *
     * @param {!function(number, number)} fn
     * @param {!Array<number>=} itemSet
     */
    _iterateItems: function(fn, itemSet) {
      var pidx, vidx, rtn, i;

      if (arguments.length === 2 && itemSet) {
        for (i = 0; i < itemSet.length; i++) {
          pidx = itemSet[i];
          if (pidx >= this._physicalStart) {
            vidx = this._virtualStart + (pidx - this._physicalStart);
          } else {
            vidx = this._virtualStart + (this._physicalCount - this._physicalStart) + pidx;
          }
          if ((rtn = fn.call(this, pidx, vidx)) != null) {
            return rtn;
          }
        }
      } else {
        pidx = this._physicalStart;
        vidx = this._virtualStart;

        for (; pidx < this._physicalCount; pidx++, vidx++) {
          if ((rtn = fn.call(this, pidx, vidx)) != null) {
            return rtn;
          }
        }
        for (pidx = 0; pidx < this._physicalStart; pidx++, vidx++) {
          if ((rtn = fn.call(this, pidx, vidx)) != null) {
            return rtn;
          }
        }
      }
    },

    /**
     * Assigns the data models to a given set of items.
     * @param {!Array<number>=} itemSet
     */
    _assignModels: function(itemSet) {
      this._iterateItems(function(pidx, vidx) {
        var el = this._physicalItems[pidx];
        var inst = el._templateInstance;
        var item = this.items && this.items[vidx];

        if (item != null) {
          inst[this.as] = item;
          inst.__key__ = this._collection.getKey(item);
          inst[this.selectedAs] = /** @type {!ArraySelectorElement} */ (this.$.selector).isSelected(item);
          inst[this.indexAs] = vidx;
          inst.tabIndex = this._focusedIndex === vidx ? 0 : -1;
          this._physicalIndexForKey[inst.__key__] = pidx;
          el.removeAttribute('hidden');
        } else {
          inst.__key__ = null;
          el.setAttribute('hidden', '');
        }
      }, itemSet);
    },

    /**
     * Updates the height for a given set of items.
     *
     * @param {!Array<number>=} itemSet
     */
     _updateMetrics: function(itemSet) {
      // Make sure we distributed all the physical items
      // so we can measure them
      Polymer.dom.flush();

      var newPhysicalSize = 0;
      var oldPhysicalSize = 0;
      var prevAvgCount = this._physicalAverageCount;
      var prevPhysicalAvg = this._physicalAverage;

      this._iterateItems(function(pidx, vidx) {

        oldPhysicalSize += this._physicalSizes[pidx] || 0;
        this._physicalSizes[pidx] = this._physicalItems[pidx].offsetHeight;
        newPhysicalSize += this._physicalSizes[pidx];
        this._physicalAverageCount += this._physicalSizes[pidx] ? 1 : 0;

      }, itemSet);

      this._physicalSize = this._physicalSize + newPhysicalSize - oldPhysicalSize;
      this._viewportSize = this._scrollTargetHeight;

      // update the average if we measured something
      if (this._physicalAverageCount !== prevAvgCount) {
        this._physicalAverage = Math.round(
            ((prevPhysicalAvg * prevAvgCount) + newPhysicalSize) /
            this._physicalAverageCount);
      }
    },

    /**
     * Updates the position of the physical items.
     */
    _positionItems: function() {
      this._adjustScrollPosition();

      var y = this._physicalTop;

      this._iterateItems(function(pidx) {
        this.translate3d(0, y + 'px', 0, this._physicalItems[pidx]);
        y += this._physicalSizes[pidx];
      });
    },

    /**
     * Adjusts the scroll position when it was overestimated.
     */
    _adjustScrollPosition: function() {
      var deltaHeight = this._virtualStart === 0 ? this._physicalTop :
          Math.min(this._scrollPosition + this._physicalTop, 0);

      if (deltaHeight) {
        this._physicalTop = this._physicalTop - deltaHeight;
        // juking scroll position during interial scrolling on iOS is no bueno
        if (!IOS_TOUCH_SCROLLING) {
          this._resetScrollPosition(this._scrollTop - deltaHeight);
        }
      }
    },

    /**
     * Sets the position of the scroll.
     */
    _resetScrollPosition: function(pos) {
      if (this.scrollTarget) {
        this._scrollTop = pos;
        this._scrollPosition = this._scrollTop;
      }
    },

    /**
     * Sets the scroll height, that's the height of the content,
     *
     * @param {boolean=} forceUpdate If true, updates the height no matter what.
     */
    _updateScrollerSize: function(forceUpdate) {
      this._estScrollHeight = (this._physicalBottom +
          Math.max(this._virtualCount - this._physicalCount - this._virtualStart, 0) * this._physicalAverage);

      forceUpdate = forceUpdate || this._scrollHeight === 0;
      forceUpdate = forceUpdate || this._scrollPosition >= this._estScrollHeight - this._physicalSize;

      // amortize height adjustment, so it won't trigger repaints very often
      if (forceUpdate || Math.abs(this._estScrollHeight - this._scrollHeight) >= this._optPhysicalSize) {
        this.$.items.style.height = this._estScrollHeight + 'px';
        this._scrollHeight = this._estScrollHeight;
      }
    },
    /**
     * Scroll to a specific item in the virtual list regardless
     * of the physical items in the DOM tree.
     *
     * @method scrollToIndex
     * @param {number} idx The index of the item
     */
    scrollToIndex: function(idx) {
      if (typeof idx !== 'number') {
        return;
      }

      Polymer.dom.flush();

      idx = Math.min(Math.max(idx, 0), this._virtualCount-1);
      // update the virtual start only when needed
      if (!this._isIndexRendered(idx) || idx >= this._maxVirtualStart) {
        this._virtualStart = idx - 1;
      }
      // manage focus
      this._manageFocus();
      // assign new models
      this._assignModels();
      // measure the new sizes
      this._updateMetrics();
      // estimate new physical offset
      this._physicalTop = this._virtualStart * this._physicalAverage;

      var currentTopItem = this._physicalStart;
      var currentVirtualItem = this._virtualStart;
      var targetOffsetTop = 0;
      var hiddenContentSize = this._hiddenContentSize;

      // scroll to the item as much as we can
      while (currentVirtualItem < idx && targetOffsetTop < hiddenContentSize) {
        targetOffsetTop = targetOffsetTop + this._physicalSizes[currentTopItem];
        currentTopItem = (currentTopItem + 1) % this._physicalCount;
        currentVirtualItem++;
      }
      // update the scroller size
      this._updateScrollerSize(true);
      // update the position of the items
      this._positionItems();
      // set the new scroll position
      this._resetScrollPosition(this._physicalTop + this._scrollerPaddingTop + targetOffsetTop + 1);
      // increase the pool of physical items if needed
      this._increasePoolIfNeeded();
      // clear cached visible index
      this._firstVisibleIndexVal = null;
      this._lastVisibleIndexVal = null;
    },

    /**
     * Reset the physical average and the average count.
     */
    _resetAverage: function() {
      this._physicalAverage = 0;
      this._physicalAverageCount = 0;
    },

    /**
     * A handler for the `iron-resize` event triggered by `IronResizableBehavior`
     * when the element is resized.
     */
    _resizeHandler: function() {
      // iOS fires the resize event when the address bar slides up
      if (IOS && Math.abs(this._viewportSize - this._scrollTargetHeight) < 100) {
        return;
      }
      this._debounceTemplate(function() {
        this._render();
        if (this._itemsRendered && this._physicalItems && this._isVisible) {
          this._resetAverage();
          this.updateViewportBoundaries();
          this.scrollToIndex(this.firstVisibleIndex);
        }
      });
    },

    _getModelFromItem: function(item) {
      var key = this._collection.getKey(item);
      var pidx = this._physicalIndexForKey[key];

      if (pidx != null) {
        return this._physicalItems[pidx]._templateInstance;
      }
      return null;
    },

    /**
     * Gets a valid item instance from its index or the object value.
     *
     * @param {(Object|number)} item The item object or its index
     */
    _getNormalizedItem: function(item) {
      if (this._collection.getKey(item) === undefined) {
        if (typeof item === 'number') {
          item = this.items[item];
          if (!item) {
            throw new RangeError('<item> not found');
          }
          return item;
        }
        throw new TypeError('<item> should be a valid item');
      }
      return item;
    },

    /**
     * Select the list item at the given index.
     *
     * @method selectItem
     * @param {(Object|number)} item The item object or its index
     */
    selectItem: function(item) {
      item = this._getNormalizedItem(item);
      var model = this._getModelFromItem(item);

      if (!this.multiSelection && this.selectedItem) {
        this.deselectItem(this.selectedItem);
      }
      if (model) {
        model[this.selectedAs] = true;
      }
      this.$.selector.select(item);
      this.updateSizeForItem(item);
    },

    /**
     * Deselects the given item list if it is already selected.
     *

     * @method deselect
     * @param {(Object|number)} item The item object or its index
     */
    deselectItem: function(item) {
      item = this._getNormalizedItem(item);
      var model = this._getModelFromItem(item);

      if (model) {
        model[this.selectedAs] = false;
      }
      this.$.selector.deselect(item);
      this.updateSizeForItem(item);
    },

    /**
     * Select or deselect a given item depending on whether the item
     * has already been selected.
     *
     * @method toggleSelectionForItem
     * @param {(Object|number)} item The item object or its index
     */
    toggleSelectionForItem: function(item) {
      item = this._getNormalizedItem(item);
      if (/** @type {!ArraySelectorElement} */ (this.$.selector).isSelected(item)) {
        this.deselectItem(item);
      } else {
        this.selectItem(item);
      }
    },

    /**
     * Clears the current selection state of the list.
     *
     * @method clearSelection
     */
    clearSelection: function() {
      function unselect(item) {
        var model = this._getModelFromItem(item);
        if (model) {
          model[this.selectedAs] = false;
        }
      }

      if (Array.isArray(this.selectedItems)) {
        this.selectedItems.forEach(unselect, this);
      } else if (this.selectedItem) {
        unselect.call(this, this.selectedItem);
      }

      /** @type {!ArraySelectorElement} */ (this.$.selector).clearSelection();
    },

    /**
     * Add an event listener to `tap` if `selectionEnabled` is true,
     * it will remove the listener otherwise.
     */
    _selectionEnabledChanged: function(selectionEnabled) {
      var handler = selectionEnabled ? this.listen : this.unlisten;
      handler.call(this, this, 'tap', '_selectionHandler');
    },

    /**
     * Select an item from an event object.
     */
    _selectionHandler: function(e) {
      if (this.selectionEnabled) {
        var model = this.modelForElement(e.target);
        if (model) {
          this.toggleSelectionForItem(model[this.as]);
        }
      }
    },

    _multiSelectionChanged: function(multiSelection) {
      this.clearSelection();
      this.$.selector.multi = multiSelection;
    },

    /**
     * Updates the size of an item.
     *
     * @method updateSizeForItem
     * @param {(Object|number)} item The item object or its index
     */
    updateSizeForItem: function(item) {
      item = this._getNormalizedItem(item);
      var key = this._collection.getKey(item);
      var pidx = this._physicalIndexForKey[key];

      if (pidx != null) {
        this._updateMetrics([pidx]);
        this._positionItems();
      }
    },

    /**
     * Creates a temporary backfill item in the rendered pool of physical items
     * to replace the main focused item. The focused item has tabIndex = 0
     * and might be currently focused by the user.
     *
     * This dynamic replacement helps to preserve the focus state.
     */
    _manageFocus: function() {
      var fidx = this._focusedIndex;

      if (fidx >= 0 && fidx < this._virtualCount) {
        // if it's a valid index, check if that index is rendered
        // in a physical item.
        if (this._isIndexRendered(fidx)) {
          this._restoreFocusedItem();
        } else {
          this._createFocusBackfillItem();
        }
      } else if (this._virtualCount > 0 && this._physicalCount > 0) {
        // otherwise, assign the initial focused index.
        this._focusedIndex = this._virtualStart;
        this._focusedItem = this._physicalItems[this._physicalStart];
      }
    },

    _isIndexRendered: function(idx) {
      return idx >= this._virtualStart && idx <= this._virtualEnd;
    },

    _isIndexVisible: function(idx) {
      return idx >= this.firstVisibleIndex && idx <= this.lastVisibleIndex;
    },

    _getPhysicalIndex: function(idx) {
      return this._physicalIndexForKey[this._collection.getKey(this._getNormalizedItem(idx))];
    },

    _focusPhysicalItem: function(idx) {
      if (idx < 0 || idx >= this._virtualCount) {
        return;
      }
      this._restoreFocusedItem();
      // scroll to index to make sure it's rendered
      if (!this._isIndexRendered(idx)) {
        this.scrollToIndex(idx);
      }

      var physicalItem = this._physicalItems[this._getPhysicalIndex(idx)];
      var SECRET = ~(Math.random() * 100);
      var model = physicalItem._templateInstance;
      var focusable;

      // set a secret tab index
      model.tabIndex = SECRET;
      // check if focusable element is the physical item
      if (physicalItem.tabIndex === SECRET) {
       focusable = physicalItem;
      }
      // search for the element which tabindex is bound to the secret tab index
      if (!focusable) {
        focusable = Polymer.dom(physicalItem).querySelector('[tabindex="' + SECRET + '"]');
      }
      // restore the tab index
      model.tabIndex = 0;
      // focus the focusable element
      this._focusedIndex = idx;
      focusable && focusable.focus();
    },

    _removeFocusedItem: function() {
      if (this._offscreenFocusedItem) {
        Polymer.dom(this).removeChild(this._offscreenFocusedItem);
      }
      this._offscreenFocusedItem = null;
      this._focusBackfillItem = null;
      this._focusedItem = null;
      this._focusedIndex = -1;
    },

    _createFocusBackfillItem: function() {
      var pidx, fidx = this._focusedIndex;
      if (this._offscreenFocusedItem || fidx < 0) {
        return;
      }
      if (!this._focusBackfillItem) {
        // create a physical item, so that it backfills the focused item.
        var stampedTemplate = this.stamp(null);
        this._focusBackfillItem = stampedTemplate.root.querySelector('*');
        Polymer.dom(this).appendChild(stampedTemplate.root);
      }
      // get the physical index for the focused index
      pidx = this._getPhysicalIndex(fidx);

      if (pidx != null) {
        // set the offcreen focused physical item
        this._offscreenFocusedItem = this._physicalItems[pidx];
        // backfill the focused physical item
        this._physicalItems[pidx] = this._focusBackfillItem;
        // hide the focused physical
        this.translate3d(0, HIDDEN_Y, 0, this._offscreenFocusedItem);
      }
    },

    _restoreFocusedItem: function() {
      var pidx, fidx = this._focusedIndex;

      if (!this._offscreenFocusedItem || this._focusedIndex < 0) {
        return;
      }
      // assign models to the focused index
      this._assignModels();
      // get the new physical index for the focused index
      pidx = this._getPhysicalIndex(fidx);

      if (pidx != null) {
        // flip the focus backfill
        this._focusBackfillItem = this._physicalItems[pidx];
        // restore the focused physical item
        this._physicalItems[pidx] = this._offscreenFocusedItem;
        // reset the offscreen focused item
        this._offscreenFocusedItem = null;
        // hide the physical item that backfills
        this.translate3d(0, HIDDEN_Y, 0, this._focusBackfillItem);
      }
    },

    _didFocus: function(e) {
      var targetModel = this.modelForElement(e.target);
      var focusedModel = this._focusedItem ? this._focusedItem._templateInstance : null;
      var hasOffscreenFocusedItem = this._offscreenFocusedItem !== null;
      var fidx = this._focusedIndex;

      if (!targetModel || !focusedModel) {
        return;
      }
      if (focusedModel === targetModel) {
        // if the user focused the same item, then bring it into view if it's not visible
        if (!this._isIndexVisible(fidx)) {
          this.scrollToIndex(fidx);
        }
      } else {
        this._restoreFocusedItem();
        // restore tabIndex for the currently focused item
        focusedModel.tabIndex = -1;
        // set the tabIndex for the next focused item
        targetModel.tabIndex = 0;
        fidx = targetModel[this.indexAs];
        this._focusedIndex = fidx;
        this._focusedItem = this._physicalItems[this._getPhysicalIndex(fidx)];

        if (hasOffscreenFocusedItem && !this._offscreenFocusedItem) {
          this._update();
        }
      }
    },

    _didMoveUp: function() {
      this._focusPhysicalItem(this._focusedIndex - 1);
    },

    _didMoveDown: function() {
      this._focusPhysicalItem(this._focusedIndex + 1);
    },

    _didEnter: function(e) {
      this._focusPhysicalItem(this._focusedIndex);
      this._selectionHandler(e.detail.keyboardEvent);
    }
  });

})();