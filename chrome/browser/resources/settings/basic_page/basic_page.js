// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview
 * 'settings-basic-page' is the settings page containing the basic settings.
 *
 * Example:
 *
 *    <iron-animated-pages>
 *      <settings-basic-page prefs="{{prefs}}"></settings-basic-page>
 *      ... other pages ...
 *    </iron-animated-pages>
 */
Polymer({
  is: 'settings-basic-page',

  properties: {
    /**
     * Preferences state.
     */
    prefs: {
      type: Object,
      notify: true,
    },

    /**
     * The current active route.
     */
    currentRoute: {
      type: Object,
      notify: true,
    },

    /**
     * True if the basic page should currently display the reset profile banner.
     * @private {boolean}
     */
    showResetProfileBanner_: {
      type: Boolean,
      value: function() {
        return loadTimeData.getBoolean('showResetProfileBanner');
      },
    },

  },

  /**
   * @type {string} Selector to get the sections.
   * TODO(michaelpg): replace duplicate docs with @override once b/24294625
   * is fixed.
   */
  sectionSelector: 'settings-section',

  /** @override */
  attached: function() {
    /** @override */
    this.scroller = this.parentElement;
  },

  behaviors: [I18nBehavior, SettingsPageVisibility, RoutableBehavior],

  onResetDone_: function() {
    this.showResetProfileBanner_ = false;
  },
});
