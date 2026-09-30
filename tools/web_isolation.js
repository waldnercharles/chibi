// The optional threaded bundle can run on static hosts without custom headers.
// Its service worker covers only this bundle's directory and stores no assets.
if (typeof document === 'undefined') {
    self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
    self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
    self.addEventListener('fetch', event => {
        if (new URL(event.request.url).origin !== self.location.origin) return;
        if (event.request.cache === 'only-if-cached' && event.request.mode !== 'same-origin') return;
        event.respondWith(fetch(event.request).then(response => {
            if (!response.status) return response;
            const headers = new Headers(response.headers);
            headers.set('Cross-Origin-Opener-Policy', 'same-origin');
            headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
            return new Response(response.body, {
                status: response.status, statusText: response.statusText, headers,
            });
        }));
    });
} else {
    (async () => {
        if (!window.crossOriginIsolated) {
            if (!window.isSecureContext || !navigator.serviceWorker) {
                throw new Error('This browser cannot enable workers here. Open the single-threaded build.');
            }
            const worker = new URL('chibi-isolation.js', window.location.href);
            if (navigator.serviceWorker.controller?.scriptURL === worker.href) {
                throw new Error('Browser isolation is unavailable. Open the single-threaded build.');
            }
            const controlled = new Promise(resolve => {
                navigator.serviceWorker.addEventListener('controllerchange', resolve, { once: true });
            });
            await navigator.serviceWorker.register(worker.href);
            await navigator.serviceWorker.ready;
            if (navigator.serviceWorker.controller?.scriptURL !== worker.href) await controlled;
            window.location.reload();
            return;
        }
        const script = document.createElement('script');
        script.src = 'chibi.js';
        script.async = true;
        document.body.append(script);
    })().catch(error => Module['showError'](error.message));
}
