// The same launch options as native, scoped to explicit URL parameters.
if (typeof window !== 'undefined') {
    const launch = new URLSearchParams(window.location.search);
    const args = Module['arguments'] || [];
    if (launch.has('play')) args.push('--play');
    if (launch.has('design-preview')) args.push('--design-preview');
    for (const name of ['project-root', 'scene', 'frames', 'fixed-dt']) {
        if (launch.has(name)) args.push('--' + name, launch.get(name));
    }
    Module['arguments'] = args;
}
