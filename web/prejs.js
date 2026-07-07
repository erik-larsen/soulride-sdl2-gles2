// Soul Ride web bootstrap: turn URL query params into game arguments.
//   soulride.html?mountain=Jay_Peak
Module['arguments'] = (function () {
  var params = new URLSearchParams(window.location.search);
  var mtn = params.get('mountain') || 'testing';
  var args = ['DefaultMountain=' + mtn];
  params.forEach(function (value, key) {
    if (key !== 'mountain') args.push(key + '=' + value);
  });
  return args;
})();

// Persist player profiles and highscores (written to /PlayerData)
// across sessions via IndexedDB.  The initial sync-in must finish
// before main() runs.
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  FS.mkdir('/PlayerData');
  FS.mount(IDBFS, {}, '/PlayerData');
  addRunDependency('idbfs-sync-in');
  FS.syncfs(true, function (err) {
    if (err) console.warn('Soul Ride: IDBFS load failed:', err);
    removeRunDependency('idbfs-sync-in');
  });
});

// Best-effort flush when the tab is hidden or closing.
window.addEventListener('visibilitychange', function () {
  if (document.visibilityState === 'hidden' && typeof FS !== 'undefined') {
    try { FS.syncfs(false, function () {}); } catch (e) {}
  }
});
