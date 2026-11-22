/* Navtex · tema claro, oscuro o automático. Se carga antes de pintar la página.
 * SPDX-License-Identifier: GPL-3.0-or-later · Copyright (c) 2026 Manuel - EA7JRS */
(function () {
  'use strict';
  var t = null;
  try { t = localStorage.getItem('navtex-tema'); } catch (e) { /* sin almacenamiento: automático */ }
  if (t === 'claro' || t === 'oscuro') document.documentElement.setAttribute('data-tema', t);
})();
