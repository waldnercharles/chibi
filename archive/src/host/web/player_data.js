// An exported game keeps its artifact, resources and engine files in data/,
// which files.json lists. Fetch them all into /data before main runs,
// where the player looks for its bundle.
Module['preRun'] = [].concat(Module['preRun'] || [], () => {
  const fetchBytes = async url => {
    const response = await fetch(url);
    if (!response.ok) throw new Error(url + ': ' + response.status + ' ' + response.statusText);
    return new Uint8Array(await response.arrayBuffer());
  };
  const load = async () => {
    const files = JSON.parse(new TextDecoder().decode(await fetchBytes('files.json')));
    const url = path => 'data/' + path.split('/').map(encodeURIComponent).join('/');
    await Promise.all(files.map(async path => {
      const target = '/data/' + path;
      const bytes = await fetchBytes(url(path));
      FS.mkdirTree(target.slice(0, target.lastIndexOf('/')));
      FS.writeFile(target, bytes);
    }));
  };
  addRunDependency('chibi-data');
  load().then(
    () => removeRunDependency('chibi-data'),
    error => Module['chibiFail']('Could not load the game: ' + error.message)
  );
});
