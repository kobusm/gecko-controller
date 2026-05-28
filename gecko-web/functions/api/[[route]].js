/**
 * Cloudflare Pages Function — API proxy
 *
 * All requests to /api/* are forwarded to the AWS API Gateway,
 * with the x-api-key header injected from a Cloudflare Pages secret.
 *
 * Required environment variables (set in Cloudflare Pages dashboard):
 *   API_BASE  — e.g. https://u0sk00gsbi.execute-api.eu-west-1.amazonaws.com/prod
 *   API_KEY   — the x-api-key value (never exposed to the browser)
 */

export async function onRequest(context) {
  const { request, env } = context;

  if (!env.API_BASE || !env.API_KEY) {
    return new Response(
      JSON.stringify({ error: 'API_BASE and API_KEY env vars must be set in Cloudflare Pages' }),
      { status: 500, headers: { 'Content-Type': 'application/json' } }
    );
  }

  // Strip the leading /api prefix, keep everything else (path + query string)
  const url = new URL(request.url);
  const apiPath = url.pathname.replace(/^\/api/, '');
  const targetUrl = `${env.API_BASE.replace(/\/$/, '')}${apiPath}${url.search}`;

  // Forward the request, injecting the API key
  const headers = new Headers();
  // Copy safe incoming headers (Content-Type for PUT/POST bodies)
  const contentType = request.headers.get('Content-Type');
  if (contentType) headers.set('Content-Type', contentType);

  headers.set('x-api-key', env.API_KEY);

  const upstream = new Request(targetUrl, {
    method:  request.method,
    headers,
    body: ['GET', 'HEAD'].includes(request.method) ? null : request.body,
  });

  const response = await fetch(upstream);

  // Pass the response through, adding CORS headers so the page can read it
  const responseHeaders = new Headers(response.headers);
  responseHeaders.set('Access-Control-Allow-Origin', '*');

  return new Response(response.body, {
    status:  response.status,
    headers: responseHeaders,
  });
}
