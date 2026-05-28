'use strict';

const { badRequest, notFound, serverError } = require('./db');
const device   = require('./device');
const settings = require('./settings');
const telemetry = require('./telemetry');
const energy   = require('./energy');

exports.handler = async (event) => {
  const { httpMethod, resource, pathParameters, queryStringParameters, body } = event;
  const deviceId = pathParameters?.deviceId;
  const query    = queryStringParameters || {};

  if (!deviceId) return badRequest('Missing deviceId');

  let parsed = {};
  if (body) {
    try { parsed = JSON.parse(body); }
    catch { return badRequest('Invalid JSON body'); }
  }

  try {
    switch (`${httpMethod} ${resource}`) {
      // ── Device → Server ──────────────────────────────────────────────────
      case 'POST /device/{deviceId}/telemetry':
        return await telemetry.ingest(deviceId, parsed);

      // Settings: GET clears the resetESP flag so the device picks it up once
      case 'GET /device/{deviceId}/settings':
        return await settings.get(deviceId);

      // ── iOS App → Server ─────────────────────────────────────────────────
      case 'PUT /device/{deviceId}/settings':
        return await settings.update(deviceId, parsed);

      case 'GET /device/{deviceId}/state':
        return await device.getState(deviceId);

      case 'GET /device/{deviceId}/power':
        return await device.getPower(deviceId);

      case 'GET /device/{deviceId}/energy':
        return await energy.get(deviceId);

      case 'GET /device/{deviceId}/history':
        return await telemetry.getHistory(deviceId, query);

      case 'POST /device/{deviceId}/reset':
        return await device.reset(deviceId);

      default:
        return notFound(`No route for ${httpMethod} ${resource}`);
    }
  } catch (err) {
    console.error({ deviceId, route: `${httpMethod} ${resource}`, err });
    return serverError(err.message);
  }
};
