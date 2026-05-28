'use strict';

const { GetCommand, UpdateCommand } = require('@aws-sdk/lib-dynamodb');
const { db, TABLES, ok, badRequest, notFound } = require('./db');

const VALID_MODES = ['off', 'pvOnly', 'acOnly', 'PVAC'];

const DEFAULTS = {
  mode:         'off',
  acThreshold:  55,
  pvThreshold:  65,
  kwhRate:      0,
  timers: {
    timer1: null,
    timer2: null,
    timer3: null,
    timer4: null,
  },
};

// Fields the iOS app is allowed to change
const ALLOWED = new Set(['mode', 'acThreshold', 'pvThreshold', 'kwhRate', 'timers']);

// ── Validation helpers ────────────────────────────────────────────────────────

function validateSettings(updates) {
  if (updates.mode !== undefined && !VALID_MODES.includes(updates.mode))
    return `mode must be one of: ${VALID_MODES.join(', ')}`;

  for (const key of ['acThreshold', 'pvThreshold']) {
    if (updates[key] !== undefined) {
      const v = updates[key];
      if (typeof v !== 'number' || v < 30 || v > 85)
        return `${key} must be a number between 30 and 85°C`;
    }
  }

  if (updates.kwhRate !== undefined && (typeof updates.kwhRate !== 'number' || updates.kwhRate < 0))
    return 'kwhRate must be a non-negative number';

  if (updates.timers !== undefined && typeof updates.timers !== 'object')
    return 'timers must be an object';

  return null;
}

// ── GET /device/{deviceId}/settings ──────────────────────────────────────────
// Called by the ESP32 every poll cycle. Returns current settings + any pending
// resetESP flag, then atomically clears resetESP so the command fires only once.

exports.get = async (deviceId) => {
  const res = await db.send(new GetCommand({
    TableName: TABLES.DEVICES,
    Key: { deviceId },
  }));

  const item = res.Item ?? {};

  const payload = {
    mode:         item.mode         ?? DEFAULTS.mode,
    acThreshold:  item.acThreshold  ?? DEFAULTS.acThreshold,
    pvThreshold:  item.pvThreshold  ?? DEFAULTS.pvThreshold,
    kwhRate:      item.kwhRate      ?? DEFAULTS.kwhRate,
    timers:       item.timers       ?? DEFAULTS.timers,
    resetESP:     item.resetESP     ?? false,
  };

  // Clear the one-shot reset flag after returning it to the device
  if (item.resetESP) {
    await db.send(new UpdateCommand({
      TableName: TABLES.DEVICES,
      Key: { deviceId },
      UpdateExpression: 'SET resetESP = :f',
      ExpressionAttributeValues: { ':f': false },
    }));
  }

  return ok(payload);
};

// ── PUT /device/{deviceId}/settings ──────────────────────────────────────────
// Called by the iOS app to change one or more settings.

exports.update = async (deviceId, updates) => {
  const validationError = validateSettings(updates);
  if (validationError) return badRequest(validationError);

  const fields = Object.keys(updates).filter(k => ALLOWED.has(k));
  if (!fields.length) return badRequest(`No recognised settings. Allowed: ${[...ALLOWED].join(', ')}`);

  // Build a dynamic SET expression to update only the supplied fields
  const setParts = fields.map((k, i) => `#f${i} = :v${i}`);
  const names    = Object.fromEntries(fields.map((k, i) => [`#f${i}`, k]));
  const values   = Object.fromEntries(fields.map((k, i) => [`:v${i}`, updates[k]]));

  await db.send(new UpdateCommand({
    TableName: TABLES.DEVICES,
    Key: { deviceId },
    UpdateExpression: `SET ${setParts.join(', ')}`,
    ExpressionAttributeNames:  names,
    ExpressionAttributeValues: values,
  }));

  return ok({ ok: true, updated: fields });
};
